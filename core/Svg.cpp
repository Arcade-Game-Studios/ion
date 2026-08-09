#include <ion/render/Svg.hpp>

#include <ion/core/Log.hpp>
#include <ion/render/Renderer.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <utility>

namespace ion {

namespace {

constexpr float kPi = 3.14159265358979323846f;

//
// 2x3 affine transform (SVG transform lists). Stored as
//   [ a c e ]
//   [ b d f ]
// applied to a column vector (x, y, 1): x' = a*x + c*y + e.
//
struct Mat3 {
    // 2x3 affine transform as [a c e; b d f] (see apply).
    float a = 1.0f, b = 0.0f, c = 0.0f;
    float d = 1.0f, e = 0.0f, f = 0.0f;

    Vector2 apply(const Vector2& p) const {
        return Vector2(a * p.x + c * p.y + e, b * p.x + d * p.y + f);
    }

    Mat3 mul(const Mat3& o) const { // this * o
        Mat3 r;
        r.a = a * o.a + c * o.b;
        r.b = b * o.a + d * o.b;
        r.c = a * o.c + c * o.d;
        r.d = b * o.c + d * o.d;
        r.e = a * o.e + c * o.f + e;
        r.f = b * o.e + d * o.f + f;
        return r;
    }
};

struct Style {
    bool hasFill = false;
    bool hasStroke = false;
    bool fillSet = false;   // fill explicitly given at this or an ancestor
    bool strokeSet = false; // stroke explicitly given at this or an ancestor
    Color fill = Color::black();
    Color stroke = Color::black();
    float strokeWidth = 1.0f;
    float fillOpacity = 1.0f;
    float strokeOpacity = 1.0f;
    float opacity = 1.0f;
};

struct Subpath {
    std::vector<Vector2> pts;
    bool closed = false;
};

//
// Markup tokenizer
//

struct Token {
    bool closing = false;
    bool selfClosing = false;
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;
};

struct AttrMap {
    std::map<std::string, std::string> values;

    bool has(const std::string& key) const {
        return values.find(key) != values.end();
    }

    const std::string* get(const std::string& key) const {
        auto it = values.find(key);
        return (it != values.end()) ? &it->second : nullptr;
    }
};

bool isNameChar(char c) {
    return std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == ':';
}

std::string readName(const std::string& s, size_t& i) {
    size_t start = i;
    while (i < s.size() && isNameChar(s[i])) {
        i++;
    }
    return s.substr(start, i - start);
}

std::string readQuoted(const std::string& s, size_t& i, char quote) {
    size_t start = ++i;
    while (i < s.size() && s[i] != quote) {
        i++;
    }
    std::string value = s.substr(start, i - start);
    if (i < s.size()) {
        i++; // consume closing quote
    }
    return value;
}

// Parses one element: "<name attrs>" or "<name attrs/>". Assumes the
// leading '<' has been consumed. Returns false for malformed input.
bool parseTag(const std::string& s, size_t& i, Token& token) {
    token = Token();
    if (i >= s.size()) {
        return false;
    }
    if (s[i] == '/') {
        token.closing = true;
        i++;
        token.name = readName(s, i);
        while (i < s.size() && s[i] != '>') {
            i++;
        }
        if (i < s.size()) {
            i++;
        }
        return !token.name.empty();
    }
    token.name = readName(s, i);
    if (token.name.empty()) {
        return false;
    }
    while (i < s.size()) {
        // Skip whitespace.
        while (i < s.size() &&
               (std::isspace((unsigned char)s[i]) || s[i] == '\n')) {
            i++;
        }
        if (i >= s.size()) {
            break;
        }
        if (s[i] == '>') {
            i++;
            break;
        }
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '>') {
            token.selfClosing = true;
            i += 2;
            break;
        }
        if (s[i] == '/' || s[i] == '>') {
            i++;
            break;
        }
        std::string key = readName(s, i);
        while (i < s.size() &&
               (std::isspace((unsigned char)s[i]) || s[i] == '=')) {
            i++;
        }
        std::string value;
        if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
            value = readQuoted(s, i, s[i]);
        } else {
            while (i < s.size() &&
                   !std::isspace((unsigned char)s[i]) && s[i] != '>' &&
                   s[i] != '/') {
                value += s[i];
                i++;
            }
        }
        if (!key.empty()) {
            token.attrs.push_back({key, value});
        }
    }
    return true;
}

// Splits the raw markup into element tokens, skipping comments, CDATA and
// processing instructions. Text content between tags is discarded.
bool tokenize(const std::string& s, std::vector<Token>& out) {
    size_t i = 0;
    while (i < s.size()) {
        size_t lt = s.find('<', i);
        if (lt == std::string::npos) {
            break;
        }
        // Comment.
        if (s.compare(lt, 4, "<!--") == 0) {
            size_t end = s.find("-->", lt + 4);
            i = (end == std::string::npos) ? s.size() : end + 3;
            continue;
        }
        // Processing instruction / doctype.
        if (lt + 1 < s.size() &&
            (s[lt + 1] == '?' || s[lt + 1] == '!')) {
            size_t end = s.find('>', lt);
            i = (end == std::string::npos) ? s.size() : end + 1;
            continue;
        }
        size_t cursor = lt + 1;
        Token token;
        if (!parseTag(s, cursor, token)) {
            return false;
        }
        out.push_back(std::move(token));
        i = cursor;
    }
    return true;
}

//
// Attribute value parsing
//

std::vector<float> parseFloats(const std::string& value) {
    std::vector<float> result;
    const char* p = value.c_str();
    char* end = nullptr;
    while (*p) {
        while (*p && (std::isspace((unsigned char)*p) || *p == ',')) {
            p++;
        }
        if (!*p) {
            break;
        }
        float v = std::strtof(p, &end);
        if (end == p) {
            break;
        }
        result.push_back(v);
        p = end;
    }
    return result;
}

bool parseColor(const std::string& value, Color& out) {
    std::string v = value;
    // Strip whitespace.
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](unsigned char c) { return std::isspace(c); }),
            v.end());
    if (v.empty()) {
        return false;
    }
    if (v[0] == '#') {
        auto hexVal = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        std::string h = v.substr(1);
        if (h.size() == 3 || h.size() == 4) {
            int r = hexVal(h[0]), g = hexVal(h[1]), b = hexVal(h[2]);
            if (r < 0 || g < 0 || b < 0) {
                return false;
            }
            int a = (h.size() == 4) ? hexVal(h[3]) : 15;
            if (a < 0) {
                return false;
            }
            out = Color::fromRGB((uint8_t)(r * 17), (uint8_t)(g * 17),
                                 (uint8_t)(b * 17), (uint8_t)(a * 17));
            return true;
        }
        if (h.size() == 6 || h.size() == 8) {
            auto byte = [&](size_t idx) -> int {
                return hexVal(h[idx]) * 16 + hexVal(h[idx + 1]);
            };
            int r = byte(0), g = byte(2), b = byte(4);
            if (r < 0 || g < 0 || b < 0) {
                return false;
            }
            int a = 255;
            if (h.size() == 8) {
                a = byte(6);
                if (a < 0) {
                    return false;
                }
            }
            out = Color::fromRGB((uint8_t)r, (uint8_t)g, (uint8_t)b,
                                 (uint8_t)a);
            return true;
        }
        return false;
    }
    struct NamedColor {
        const char* name;
        uint8_t r, g, b;
    };
    static const NamedColor kNamed[] = {
        {"black", 0, 0, 0},       {"white", 255, 255, 255},
        {"red", 255, 0, 0},       {"green", 0, 128, 0},
        {"blue", 0, 0, 255},      {"yellow", 255, 255, 0},
        {"cyan", 0, 255, 255},    {"aqua", 0, 255, 255},
        {"magenta", 255, 0, 255}, {"fuchsia", 255, 0, 255},
        {"gray", 128, 128, 128},  {"grey", 128, 128, 128},
        {"silver", 192, 192, 192},{"maroon", 128, 0, 0},
        {"olive", 128, 128, 0},   {"lime", 0, 255, 0},
        {"navy", 0, 0, 128},      {"teal", 0, 128, 128},
        {"purple", 128, 0, 128},  {"orange", 255, 165, 0},
        {"brown", 165, 42, 42},   {"pink", 255, 192, 203},
    };
    std::string lower = v;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    for (const auto& named : kNamed) {
        if (lower == named.name) {
            out = Color::fromRGB(named.r, named.g, named.b);
            return true;
        }
    }
    return false;
}

// Parses style="k:v;k:v" into the attribute map (without overriding explicit
// attributes already present on the element).
void mergeStyleAttribute(AttrMap& attrs, const std::string& style) {
    size_t i = 0;
    while (i < style.size()) {
        while (i < style.size() &&
               (std::isspace((unsigned char)style[i]) || style[i] == ';')) {
            i++;
        }
        size_t colon = style.find(':', i);
        if (colon == std::string::npos) {
            break;
        }
        std::string key = style.substr(i, colon - i);
        size_t semi = style.find(';', colon + 1);
        std::string val = style.substr(
            colon + 1, (semi == std::string::npos) ? std::string::npos
                                                   : semi - colon - 1);
        // Trim whitespace.
        auto trim = [](std::string& s) {
            size_t a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) {
                s.clear();
                return;
            }
            size_t b = s.find_last_not_of(" \t\r\n");
            s = s.substr(a, b - a + 1);
        };
        trim(key);
        trim(val);
        if (!key.empty() && !val.empty()) {
            attrs.values[key] = val; // style attribute wins over presentation
        }
        if (semi == std::string::npos) {
            break;
        }
        i = semi + 1;
    }
}

Mat3 parseTransform(const std::string& value) {
    Mat3 result;
    size_t i = 0;
    while (i < value.size()) {
        while (i < value.size() &&
               (std::isspace((unsigned char)value[i]) || value[i] == ',')) {
            i++;
        }
        if (i >= value.size()) {
            break;
        }
        size_t nameStart = i;
        while (i < value.size() && value[i] != '(') {
            i++;
        }
        if (i >= value.size()) {
            break;
        }
        std::string name = value.substr(nameStart, i - nameStart);
        size_t open = i;
        size_t close = value.find(')', open);
        if (close == std::string::npos) {
            break;
        }
        std::string argsStr = value.substr(open + 1, close - open - 1);
        std::vector<float> args = parseFloats(argsStr);
        Mat3 m;
        if (name == "matrix" && args.size() >= 6) {
            m.a = args[0]; m.b = args[1]; m.c = args[2];
            m.d = args[3]; m.e = args[4]; m.f = args[5];
        } else if (name == "translate") {
            m.e = args.empty() ? 0.0f : args[0];
            m.f = args.size() < 2 ? 0.0f : args[1];
        } else if (name == "scale") {
            float sx = args.empty() ? 1.0f : args[0];
            m.a = sx;
            m.d = args.size() < 2 ? sx : args[1];
        } else if (name == "rotate") {
            if (args.empty()) {
                // nothing
            } else if (args.size() == 1) {
                float ang = args[0] * kPi / 180.0f;
                float c = std::cos(ang), s = std::sin(ang);
                m.a = c; m.b = s; m.c = -s; m.d = c;
            } else if (args.size() >= 3) {
                float ang = args[0] * kPi / 180.0f;
                float c = std::cos(ang), s = std::sin(ang);
                float cx = args[1], cy = args[2];
                // translate(cx,cy) * rotate * translate(-cx,-cy)
                m.a = c; m.b = s; m.c = -s; m.d = c;
                m.e = cx - c * cx + s * cy;
                m.f = cy - s * cx - c * cy;
            }
        } else if (name == "skewX") {
            if (!args.empty()) {
                m.c = std::tan(args[0] * kPi / 180.0f);
            }
        } else if (name == "skewY") {
            if (!args.empty()) {
                m.b = std::tan(args[0] * kPi / 180.0f);
            }
        }
        result = result.mul(m);
        i = close + 1;
    }
    return result;
}

//
// Shape construction from presentation attributes
//

struct Frame {
    Style style;
    Mat3 transform;
    bool visible = true;
};

void applyPaintAttrs(const AttrMap& attrs, Style& style) {
    auto setColor = [&](const std::string& key, bool& set, bool& has,
                        Color& out) {
        if (const std::string* v = attrs.get(key)) {
            set = true;
            if (*v == "none") {
                has = false;
            } else if (parseColor(*v, out)) {
                has = true;
            }
        }
    };
    setColor("fill", style.fillSet, style.hasFill, style.fill);
    setColor("stroke", style.strokeSet, style.hasStroke, style.stroke);
    if (const std::string* v = attrs.get("stroke-width")) {
        std::vector<float> f = parseFloats(*v);
        if (!f.empty() && f[0] >= 0.0f) {
            style.strokeWidth = f[0];
        }
    }
    if (const std::string* v = attrs.get("fill-opacity")) {
        std::vector<float> f = parseFloats(*v);
        if (!f.empty()) {
            style.fillOpacity = std::clamp(f[0], 0.0f, 1.0f);
        }
    }
    if (const std::string* v = attrs.get("stroke-opacity")) {
        std::vector<float> f = parseFloats(*v);
        if (!f.empty()) {
            style.strokeOpacity = std::clamp(f[0], 0.0f, 1.0f);
        }
    }
    if (const std::string* v = attrs.get("opacity")) {
        std::vector<float> f = parseFloats(*v);
        if (!f.empty()) {
            style.opacity = std::clamp(f[0], 0.0f, 1.0f);
        }
    }
}

// Applies element attributes (plus the style attribute) on top of the
// inherited parent style.
Style resolveStyle(const AttrMap& attrs, const Style& parent) {
    Style style = parent;
    AttrMap merged = attrs;
    if (const std::string* s = attrs.get("style")) {
        mergeStyleAttribute(merged, *s);
    }
    applyPaintAttrs(merged, style);
    // A leaf with no paint specified anywhere in its chain gets the SVG
    // default black fill.
    if (!style.fillSet && !style.strokeSet) {
        style.hasFill = true;
        style.fill = Color::black();
    }
    return style;
}

void appendRect(const AttrMap& attrs, std::vector<Subpath>& out) {
    auto getF = [&](const char* key, float dflt) {
        if (const std::string* v = attrs.get(key)) {
            std::vector<float> f = parseFloats(*v);
            if (!f.empty()) {
                return f[0];
            }
        }
        return dflt;
    };
    float px = getF("x", 0.0f);
    float py = getF("y", 0.0f);
    float pw = getF("width", 0.0f);
    float ph = getF("height", 0.0f);
    if (pw <= 0.0f || ph <= 0.0f) {
        return;
    }
    float rx = getF("rx", 0.0f);
    float ry = getF("ry", 0.0f);
    if (rx == 0.0f && ry == 0.0f) {
        Subpath sub;
        sub.pts = {Vector2(px, py), Vector2(px + pw, py),
                   Vector2(px + pw, py + ph), Vector2(px, py + ph)};
        sub.closed = true;
        out.push_back(std::move(sub));
        return;
    }
    if (rx == 0.0f) rx = ry;
    if (ry == 0.0f) ry = rx;
    rx = std::min(rx, pw * 0.5f);
    ry = std::min(ry, ph * 0.5f);
    // Rounded rect: quarter arcs approximated with line segments.
    Subpath sub;
    const int kSeg = 6;
    auto arc = [&](float cx, float cy, float start, float end) {
        for (int k = 1; k <= kSeg; k++) {
            float t = start + (end - start) * (float)k / (float)kSeg;
            sub.pts.push_back(
                Vector2(cx + rx * std::cos(t), cy + ry * std::sin(t)));
        }
    };
    float top = py + ry, bot = py + ph - ry;
    float left = px + rx, right = px + pw - rx;
    sub.pts.push_back(Vector2(px + rx, py));
    arc(right, top, -kPi / 2.0f, 0.0f);
    arc(right, bot, 0.0f, kPi / 2.0f);
    arc(left, bot, kPi / 2.0f, kPi);
    arc(left, top, kPi, kPi * 1.5f);
    sub.closed = true;
    out.push_back(std::move(sub));
}

void appendEllipse(const AttrMap& attrs, bool circle, std::vector<Subpath>& out) {
    auto getF = [&](const char* key, float dflt) {
        if (const std::string* v = attrs.get(key)) {
            std::vector<float> f = parseFloats(*v);
            if (!f.empty()) {
                return f[0];
            }
        }
        return dflt;
    };
    float x = getF("cx", 0.0f);
    float y = getF("cy", 0.0f);
    float rx, ry;
    if (circle) {
        rx = ry = getF("r", 0.0f);
    } else {
        rx = getF("rx", 0.0f);
        ry = getF("ry", 0.0f);
    }
    if (rx <= 0.0f || ry <= 0.0f) {
        return;
    }
    const int kSeg = 48;
    Subpath sub;
    sub.pts.reserve(kSeg + 1);
    for (int k = 0; k < kSeg; k++) {
        float t = kPi * 2.0f * (float)k / (float)kSeg;
        sub.pts.push_back(Vector2(x + rx * std::cos(t), y + ry * std::sin(t)));
    }
    sub.closed = true;
    out.push_back(std::move(sub));
}

void appendLine(const AttrMap& attrs, std::vector<Subpath>& out) {
    auto getF = [&](const char* key, float dflt) {
        if (const std::string* v = attrs.get(key)) {
            std::vector<float> f = parseFloats(*v);
            if (!f.empty()) {
                return f[0];
            }
        }
        return dflt;
    };
    Subpath sub;
    sub.pts = {Vector2(getF("x1", 0.0f), getF("y1", 0.0f)),
               Vector2(getF("x2", 0.0f), getF("y2", 0.0f))};
    sub.closed = false;
    out.push_back(std::move(sub));
}

void appendPoly(const AttrMap& attrs, bool polygon, std::vector<Subpath>& out) {
    const std::string* pts = attrs.get("points");
    if (!pts) {
        return;
    }
    std::vector<float> nums = parseFloats(*pts);
    if (nums.size() < 4) {
        return;
    }
    Subpath sub;
    for (size_t k = 0; k + 1 < nums.size(); k += 2) {
        sub.pts.push_back(Vector2(nums[k], nums[k + 1]));
    }
    sub.closed = polygon;
    if (sub.pts.size() >= 2) {
        out.push_back(std::move(sub));
    }
}

//
// <path d="..."> parsing
//

struct PathParser {
    const std::string& s;
    size_t i = 0;

    void skip() {
        while (i < s.size() &&
               (std::isspace((unsigned char)s[i]) || s[i] == ',')) {
            i++;
        }
    }

    bool peekNumber() {
        skip();
        if (i >= s.size()) {
            return false;
        }
        char c = s[i];
        return c == '-' || c == '+' || c == '.' ||
               std::isdigit((unsigned char)c);
    }

    float num() {
        skip();
        char* end = nullptr;
        float v = std::strtof(s.c_str() + i, &end);
        if (end != s.c_str() + i) {
            i = end - s.c_str();
        }
        return v;
    }

    char command() {
        skip();
        if (i >= s.size()) {
            return 0;
        }
        return s[i++];
    }
};

void flattenCubic(const Vector2& p0, const Vector2& p1, const Vector2& p2,
                  const Vector2& p3, std::vector<Vector2>& out) {
    const int n = 12;
    for (int k = 1; k <= n; k++) {
        float t = (float)k / (float)n;
        float u = 1.0f - t;
        float x = u * u * u * p0.x + 3.0f * u * u * t * p1.x +
                  3.0f * u * t * t * p2.x + t * t * t * p3.x;
        float y = u * u * u * p0.y + 3.0f * u * u * t * p1.y +
                  3.0f * u * t * t * p2.y + t * t * t * p3.y;
        out.push_back(Vector2(x, y));
    }
}

void flattenQuad(const Vector2& p0, const Vector2& p1, const Vector2& p2,
                 std::vector<Vector2>& out) {
    const int n = 10;
    for (int k = 1; k <= n; k++) {
        float t = (float)k / (float)n;
        float u = 1.0f - t;
        float x = u * u * p0.x + 2.0f * u * t * p1.x + t * t * p2.x;
        float y = u * u * p0.y + 2.0f * u * t * p1.y + t * t * p2.y;
        out.push_back(Vector2(x, y));
    }
}

// Flattens an elliptical arc from `cur` to `end` into line segments.
void flattenArc(const Vector2& cur, float rx, float ry, float rotDeg,
                bool largeArc, bool sweep, const Vector2& end,
                std::vector<Vector2>& out) {
    if (cur == end) {
        return;
    }
    float phi = rotDeg * kPi / 180.0f;
    float cp = std::cos(phi), sp = std::sin(phi);
    float dx = (cur.x - end.x) * 0.5f, dy = (cur.y - end.y) * 0.5f;
    float x1p = cp * dx + sp * dy;
    float y1p = -sp * dx + cp * dy;
    float ra = std::fabs(rx), rb = std::fabs(ry);
    if (ra < 1e-6f || rb < 1e-6f) {
        out.push_back(end);
        return;
    }
    float lambda = (x1p * x1p) / (ra * ra) + (y1p * y1p) / (rb * rb);
    if (lambda > 1.0f) {
        float s = std::sqrt(lambda);
        ra *= s;
        rb *= s;
    }
    float sign = (largeArc == sweep) ? -1.0f : 1.0f;
    float num = ra * ra * rb * rb - ra * ra * y1p * y1p -
                rb * rb * x1p * x1p;
    float den = ra * ra * y1p * y1p + rb * rb * x1p * x1p;
    float coef = (den > 1e-12f) ? sign * std::sqrt(std::max(0.0f, num / den))
                                : 0.0f;
    float cxp = coef * (ra * y1p / rb);
    float cyp = -coef * (rb * x1p / ra);
    float cx = cp * cxp - sp * cyp + (cur.x + end.x) * 0.5f;
    float cy = sp * cxp + cp * cyp + (cur.y + end.y) * 0.5f;
    auto angle = [](float ux, float uy, float vx, float vy) -> float {
        return std::atan2(std::fabs(ux * vy - uy * vx), ux * vx + uy * vy);
    };
    float theta1 = angle(1.0f, 0.0f, (x1p - cxp) / ra, (y1p - cyp) / rb);
    float delta = angle((x1p - cxp) / ra, (y1p - cyp) / rb,
                        (-x1p - cxp) / ra, (-y1p - cyp) / rb);
    if (!sweep && delta > 0.0f) {
        delta -= kPi * 2.0f;
    }
    if (sweep && delta < 0.0f) {
        delta += kPi * 2.0f;
    }
    int n = (int)std::ceil(std::fabs(delta) / (kPi / 8.0f));
    n = std::max(2, std::min(n, 64));
    for (int k = 1; k <= n; k++) {
        float t = theta1 + delta * (float)k / (float)n;
        float c = std::cos(t), sn = std::sin(t);
        out.push_back(Vector2(cx + ra * c * cp - rb * sn * sp,
                              cy + ra * c * sp + rb * sn * cp));
    }
}

bool parsePathData(const std::string& d, std::vector<Subpath>& out) {
    PathParser p{d};
    Vector2 cur;
    Vector2 subStart;
    std::vector<Vector2> pts;
    char lastCmd = 0;
    Vector2 cubicControl, quadControl;
    bool subOpen = false;

    auto beginSubpath = [&]() {
        if (subOpen) {
            Subpath sub;
            sub.pts = pts;
            sub.closed = false;
            out.push_back(std::move(sub));
        }
        pts.clear();
        pts.push_back(cur);
        subStart = cur;
        subOpen = true;
    };

    auto closeSubpath = [&]() {
        if (!subOpen) {
            return;
        }
        if (!pts.empty() && pts.front() == cur) {
            pts.pop_back();
        }
        Subpath sub;
        sub.pts = pts;
        sub.closed = true;
        out.push_back(std::move(sub));
        pts.clear();
        subOpen = false;
        cur = subStart;
    };

    char cmd = p.command();
    if (!cmd) {
        return true; // empty path is valid
    }
    while (cmd) {
        bool rel = (cmd >= 'a' && cmd <= 'z');
        switch (cmd) {
        case 'M':
        case 'm': {
            float x = p.num(), y = p.num();
            cur = rel ? Vector2(cur.x + x, cur.y + y) : Vector2(x, y);
            beginSubpath();
            // Subsequent coordinate pairs are implicit linetos.
            while (p.peekNumber()) {
                x = p.num();
                y = p.num();
                cur = rel ? Vector2(cur.x + x, cur.y + y) : Vector2(x, y);
                pts.push_back(cur);
                lastCmd = rel ? 'l' : 'L';
            }
            break;
        }
        case 'L':
        case 'l':
            while (p.peekNumber()) {
                float x = p.num(), y = p.num();
                cur = rel ? Vector2(cur.x + x, cur.y + y) : Vector2(x, y);
                pts.push_back(cur);
            }
            break;
        case 'H':
        case 'h': {
            float x = p.num();
            cur = rel ? Vector2(cur.x + x, cur.y) : Vector2(x, cur.y);
            pts.push_back(cur);
            break;
        }
        case 'V':
        case 'v': {
            float y = p.num();
            cur = rel ? Vector2(cur.x, cur.y + y) : Vector2(cur.x, y);
            pts.push_back(cur);
            break;
        }
        case 'C':
        case 'c':
            while (p.peekNumber()) {
                Vector2 c1(p.num(), p.num());
                Vector2 c2(p.num(), p.num());
                Vector2 e(p.num(), p.num());
                if (rel) {
                    c1 = c1 + cur;
                    c2 = c2 + cur;
                    e = e + cur;
                }
                flattenCubic(cur, c1, c2, e, pts);
                cur = e;
                cubicControl = c2;
            }
            break;
        case 'S':
        case 's':
            while (p.peekNumber()) {
                Vector2 c1 = (lastCmd == 'C' || lastCmd == 'c' ||
                              lastCmd == 'S' || lastCmd == 's')
                                 ? Vector2(2.0f * cur.x - cubicControl.x,
                                           2.0f * cur.y - cubicControl.y)
                                 : cur;
                Vector2 c2(p.num(), p.num());
                Vector2 e(p.num(), p.num());
                if (rel) {
                    c2 = c2 + cur;
                    e = e + cur;
                }
                flattenCubic(cur, c1, c2, e, pts);
                cur = e;
                cubicControl = c2;
            }
            break;
        case 'Q':
        case 'q':
            while (p.peekNumber()) {
                Vector2 c1(p.num(), p.num());
                Vector2 e(p.num(), p.num());
                if (rel) {
                    c1 = c1 + cur;
                    e = e + cur;
                }
                flattenQuad(cur, c1, e, pts);
                cur = e;
                quadControl = c1;
            }
            break;
        case 'T':
        case 't':
            while (p.peekNumber()) {
                Vector2 c1 = (lastCmd == 'Q' || lastCmd == 'q' ||
                              lastCmd == 'T' || lastCmd == 't')
                                 ? Vector2(2.0f * cur.x - quadControl.x,
                                           2.0f * cur.y - quadControl.y)
                                 : cur;
                Vector2 e(p.num(), p.num());
                if (rel) {
                    e = e + cur;
                }
                flattenQuad(cur, c1, e, pts);
                cur = e;
                quadControl = c1;
            }
            break;
        case 'A':
        case 'a':
            while (p.peekNumber()) {
                float rx = p.num(), ry = p.num();
                float rot = p.num();
                bool large = p.num() != 0.0f;
                bool sweep = p.num() != 0.0f;
                Vector2 e(p.num(), p.num());
                if (rel) {
                    e = e + cur;
                }
                flattenArc(cur, rx, ry, rot, large, sweep, e, pts);
                cur = e;
            }
            break;
        case 'Z':
        case 'z':
            closeSubpath();
            break;
        default:
            return false; // unsupported command
        }
        lastCmd = cmd;
        cmd = p.command();
    }
    if (subOpen) {
        Subpath sub;
        sub.pts = pts;
        sub.closed = false;
        out.push_back(std::move(sub));
    }
    return true;
}

void appendPath(const AttrMap& attrs, std::vector<Subpath>& out) {
    const std::string* d = attrs.get("d");
    if (!d) {
        return;
    }
    parsePathData(*d, out);
}

//
// Rasterizer
//

bool evenOddInside(const std::vector<Subpath>& subpaths, const Vector2& p) {
    bool inside = false;
    for (const auto& sub : subpaths) {
        size_t n = sub.pts.size();
        if (n < 3) {
            continue;
        }
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const Vector2& a = sub.pts[j];
            const Vector2& b = sub.pts[i];
            if ((a.y > p.y) != (b.y > p.y)) {
                float xInt =
                    a.x + (p.y - a.y) / (b.y - a.y) * (b.x - a.x);
                if (p.x < xInt) {
                    inside = !inside;
                }
            }
        }
    }
    return inside;
}

float segmentDistanceSq(const Vector2& p, const Vector2& a, const Vector2& b) {
    Vector2 ab = b - a;
    float len2 = ab.lengthSquared();
    if (len2 <= 1e-12f) {
        return (p - a).lengthSquared();
    }
    float t = Vector2::dot(p - a, ab) / len2;
    t = std::clamp(t, 0.0f, 1.0f);
    Vector2 q = a + ab * t;
    return (p - q).lengthSquared();
}

void compositePixel(uint8_t* dst, const Color& color, float alpha) {
    float sa = color.a * alpha;
    if (sa <= 0.0f) {
        return;
    }
    float sr = color.r, sg = color.g, sb = color.b;
    float dr = dst[0] / 255.0f, dg = dst[1] / 255.0f, db = dst[2] / 255.0f;
    float da = dst[3] / 255.0f;
    float oa = sa + da * (1.0f - sa);
    if (oa <= 1e-6f) {
        dst[0] = dst[1] = dst[2] = dst[3] = 0;
        return;
    }
    float or_ = (sr * sa + dr * da * (1.0f - sa)) / oa;
    float og = (sg * sa + dg * da * (1.0f - sa)) / oa;
    float ob = (sb * sa + db * da * (1.0f - sa)) / oa;
    dst[0] = (uint8_t)(or_ * 255.0f + 0.5f);
    dst[1] = (uint8_t)(og * 255.0f + 0.5f);
    dst[2] = (uint8_t)(ob * 255.0f + 0.5f);
    dst[3] = (uint8_t)(oa * 255.0f + 0.5f);
}

} // namespace

struct SvgImage::Shape {
    Style style;
    std::vector<Subpath> subpaths;
    bool fillable = true;
};

SvgImage::SvgImage() = default;

SvgImage::~SvgImage() = default;

SvgImage::SvgImage(SvgImage&& other) noexcept = default;

SvgImage& SvgImage::operator=(SvgImage&& other) noexcept = default;

bool SvgImage::parse(const std::string& svg) {
    std::vector<Token> tokens;
    if (!tokenize(svg, tokens)) {
        ION_LOG_ERROR("SvgImage: malformed markup");
        return false;
    }
    if (tokens.empty()) {
        ION_LOG_ERROR("SvgImage: no elements found");
        return false;
    }

    std::vector<Shape> shapes;
    std::vector<Frame> stack;
    uint32_t w = 0, h = 0;

    for (const Token& token : tokens) {
        AttrMap attrs;
        for (const auto& kv : token.attrs) {
            attrs.values[kv.first] = kv.second;
        }

        if (token.closing) {
            if (!stack.empty()) {
                stack.pop_back();
            }
            continue;
        }

        const std::string& name = token.name;
        bool isContainer = name == "svg" || name == "g" || name == "defs" ||
                           name == "clipPath" || name == "symbol" ||
                           name == "linearGradient" ||
                           name == "radialGradient" || name == "pattern" ||
                           name == "mask";
        bool isShape = name == "rect" || name == "circle" ||
                       name == "ellipse" || name == "line" ||
                       name == "polyline" || name == "polygon" ||
                       name == "path";

        if (name == "svg") {
            if (token.selfClosing) {
                continue;
            }
            if (const std::string* v = attrs.get("width")) {
                std::vector<float> f = parseFloats(*v);
                if (!f.empty() && f[0] >= 0.0f) w = (uint32_t)f[0];
            }
            if (const std::string* v = attrs.get("height")) {
                std::vector<float> f = parseFloats(*v);
                if (!f.empty() && f[0] >= 0.0f) h = (uint32_t)f[0];
            }
            const std::string* vb = attrs.get("viewBox");
            if ((w == 0 || h == 0) && vb) {
                std::vector<float> f = parseFloats(*vb);
                if (f.size() >= 4) {
                    if (w == 0) w = (uint32_t)f[2];
                    if (h == 0) h = (uint32_t)f[3];
                }
            }            Frame frame;
            frame.style = Style();
            frame.transform = Mat3();
            if (const std::string* v = attrs.get("transform")) {
                frame.transform = parseTransform(*v);
            }
            stack.push_back(frame);
            continue;
        }

        Frame inherited;
        inherited.style = Style();
        if (!stack.empty()) {
            inherited = stack.back();
        }

        if (isContainer) {
            if (token.selfClosing) {
                continue; // empty container, nothing to inherit from
            }
            Frame frame = inherited;
            frame.visible =
                frame.visible && (name != "defs" && name != "clipPath" &&
                                  name != "linearGradient" &&
                                  name != "radialGradient" && name != "pattern" &&
                                  name != "mask");
            if (const std::string* v = attrs.get("style")) {
                AttrMap merged = attrs;
                mergeStyleAttribute(merged, *v);
                applyPaintAttrs(merged, frame.style);
            } else {
                applyPaintAttrs(attrs, frame.style);
            }
            if (const std::string* v = attrs.get("transform")) {
                frame.transform = inherited.transform.mul(parseTransform(*v));
            }
            stack.push_back(frame);
            continue;
        }

        if (!isShape || !inherited.visible) {
            continue;
        }

        Style style = resolveStyle(attrs, inherited.style);
        if (!style.hasFill && !style.hasStroke) {
            continue; // nothing to draw
        }

        Shape shape;
        shape.style = style;
        shape.fillable = name != "line";

        if (name == "rect") {
            appendRect(attrs, shape.subpaths);
        } else if (name == "circle" || name == "ellipse") {
            appendEllipse(attrs, name == "circle", shape.subpaths);
        } else if (name == "line") {
            appendLine(attrs, shape.subpaths);
        } else if (name == "polyline" || name == "polygon") {
            appendPoly(attrs, name == "polygon", shape.subpaths);
        } else if (name == "path") {
            appendPath(attrs, shape.subpaths);
        }
        if (shape.subpaths.empty()) {
            continue;
        }

        // Bake the element transform (parent * local) into the points.
        Mat3 transform = inherited.transform;
        if (const std::string* v = attrs.get("transform")) {
            transform = transform.mul(parseTransform(*v));
        }
        if (transform.a != 1.0f || transform.b != 0.0f ||
            transform.c != 0.0f || transform.d != 1.0f ||
            transform.e != 0.0f || transform.f != 0.0f) {
            for (auto& sub : shape.subpaths) {
                for (auto& pt : sub.pts) {
                    pt = transform.apply(pt);
                }
            }
        }
        shapes.push_back(std::move(shape));
    }

    // If no size was declared, derive it from the shape bounds.
    if (w == 0 || h == 0) {
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = -std::numeric_limits<float>::max();
        float maxY = -std::numeric_limits<float>::max();
        bool any = false;
        for (const auto& shape : shapes) {
            for (const auto& sub : shape.subpaths) {
                for (const auto& pt : sub.pts) {
                    minX = std::min(minX, pt.x);
                    minY = std::min(minY, pt.y);
                    maxX = std::max(maxX, pt.x);
                    maxY = std::max(maxY, pt.y);
                    any = true;
                }
            }
        }
        if (!any) {
            ION_LOG_ERROR("SvgImage: no shapes to render");
            return false;
        }
        float pad = 2.0f;
        w = (uint32_t)std::ceil(maxX - minX + pad * 2.0f);
        h = (uint32_t)std::ceil(maxY - minY + pad * 2.0f);
    }

    shapes_ = std::move(shapes);
    width_ = w;
    height_ = h;
    valid_ = w > 0 && h > 0;
    return valid_;
}

bool SvgImage::parseFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ION_LOG_ERROR("SvgImage: cannot open '%s'", path.c_str());
        return false;
    }
    std::string contents((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    return parse(contents);
}

bool SvgImage::isValid() const {
    return valid_;
}

uint32_t SvgImage::width() const {
    return width_;
}

uint32_t SvgImage::height() const {
    return height_;
}

std::vector<uint8_t> SvgImage::rasterize() const {
    std::vector<uint8_t> out;
    if (!valid_) {
        return out;
    }
    const uint32_t w = width_, h = height_;
    out.resize((size_t)w * h * 4, 0);
    const float kSS = 2.0f; // supersampling factor

    for (const Shape& shape : shapes_) {
        const bool doFill = shape.fillable && shape.style.hasFill;
        const bool doStroke = shape.style.hasStroke &&
                              shape.style.strokeWidth > 0.0f;
        if (!doFill && !doStroke) {
            continue;
        }

        // Bounds of this shape, expanded for the stroke, clamped to image.
        float bMinX = (float)w, bMinY = (float)h, bMaxX = 0.0f, bMaxY = 0.0f;
        for (const auto& sub : shape.subpaths) {
            for (const auto& pt : sub.pts) {
                bMinX = std::min(bMinX, pt.x);
                bMinY = std::min(bMinY, pt.y);
                bMaxX = std::max(bMaxX, pt.x);
                bMaxY = std::max(bMaxY, pt.y);
            }
        }
        float pad = (doStroke ? shape.style.strokeWidth * 0.5f : 0.0f) + 1.0f;
        int x0 = std::clamp((int)std::floor(bMinX - pad), 0, (int)w - 1);
        int y0 = std::clamp((int)std::floor(bMinY - pad), 0, (int)h - 1);
        int x1 = std::clamp((int)std::ceil(bMaxX + pad), 0, (int)w - 1);
        int y1 = std::clamp((int)std::ceil(bMaxY + pad), 0, (int)h - 1);
        if (x1 < x0 || y1 < y0) {
            continue;
        }

        const float fillAlpha =
            shape.style.fillOpacity * shape.style.opacity;
        const float strokeAlpha =
            shape.style.strokeOpacity * shape.style.opacity;
        const float strokeR = shape.style.strokeWidth * 0.5f;
        const float strokeR2 = strokeR * strokeR;
        const float invSamples = 1.0f / (kSS * kSS);

        for (int py = y0; py <= y1; py++) {
            for (int px = x0; px <= x1; px++) {
                int fillHits = 0, strokeHits = 0;
                for (int sy = 0; sy < (int)kSS; sy++) {
                    for (int sx = 0; sx < (int)kSS; sx++) {
                        Vector2 p((float)px + (sx + 0.5f) / kSS,
                                  (float)py + (sy + 0.5f) / kSS);
                        if (doFill && evenOddInside(shape.subpaths, p)) {
                            fillHits++;
                        }
                        if (doStroke) {
                            bool hit = false;
                            for (const auto& sub : shape.subpaths) {
                                size_t n = sub.pts.size();
                                if (n < 2) {
                                    continue;
                                }
                                for (size_t i = 0, j = n - 1; i < n; j = i++) {
                                    if (i == 0 && !sub.closed) {
                                        continue;
                                    }
                                    if (segmentDistanceSq(p, sub.pts[j],
                                                          sub.pts[i]) <=
                                        strokeR2) {
                                        hit = true;
                                        break;
                                    }
                                }
                                if (hit) {
                                    break;
                                }
                            }
                            if (hit) {
                                strokeHits++;
                            }
                        }
                    }
                }
                uint8_t* dst = &out[((size_t)py * w + px) * 4];
                if (fillHits > 0) {
                    compositePixel(dst, shape.style.fill,
                                   fillAlpha * (float)fillHits * invSamples);
                }
                if (strokeHits > 0) {
                    compositePixel(dst, shape.style.stroke,
                                   strokeAlpha * (float)strokeHits *
                                       invSamples);
                }
            }
        }
    }
    return out;
}

Texture SvgImage::createTexture(Renderer& renderer) const {
    std::vector<uint8_t> pixels = rasterize();
    if (pixels.empty()) {
        return Texture{};
    }
    TextureDesc desc;
    desc.width = width_;
    desc.height = height_;
    desc.filterLinear = false;
    return renderer.createTexture(desc, pixels.data());
}

std::vector<uint8_t> rasterizeSvg(const std::string& svg, uint32_t& outWidth,
                                  uint32_t& outHeight) {
    SvgImage image;
    outWidth = 0;
    outHeight = 0;
    if (!image.parse(svg)) {
        return {};
    }
    outWidth = image.width();
    outHeight = image.height();
    return image.rasterize();
}

} // namespace ion
