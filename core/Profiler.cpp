#include <ion/core/Profiler.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ion {

namespace {

struct State {
    std::vector<Profiler::Section> sections;
    uint64_t frames = 0;
    bool enabled = true;
};

State& state() {
    static State s;
    return s;
}

Profiler::Section* findMutable(const char* name) {
    for (Profiler::Section& section : state().sections) {
        // Pointer equality first (literals are usually pooled), then text.
        if (section.name == name || std::strcmp(section.name, name) == 0) {
            return &section;
        }
    }
    return nullptr;
}

} // namespace

void Profiler::record(const char* name, double milliseconds) {
    State& s = state();
    if (!s.enabled || !name) {
        return;
    }
    Section* section = findMutable(name);
    if (!section) {
        Section added;
        added.name = name;
        s.sections.push_back(added);
        section = &s.sections.back();
    }
    section->currentMs += milliseconds;
    section->currentCalls++;
}

void Profiler::endFrame() {
    State& s = state();
    if (!s.enabled) {
        return;
    }
    for (Section& section : s.sections) {
        section.lastFrameMs = section.currentMs;
        section.lastFrameCalls = section.currentCalls;
        section.maxMs = std::max(section.maxMs, section.currentMs);
        // ~0.5 s of smoothing at 60 fps; the first frame seeds the average.
        section.averageMs = s.frames == 0 ? section.currentMs
                                          : section.averageMs +
                                                (section.currentMs - section.averageMs) * 0.03;
        section.currentMs = 0.0;
        section.currentCalls = 0;
    }
    s.frames++;
}

const std::vector<Profiler::Section>& Profiler::sections() {
    return state().sections;
}

const Profiler::Section* Profiler::find(const char* name) {
    return name ? findMutable(name) : nullptr;
}

uint64_t Profiler::frameCount() {
    return state().frames;
}

std::string Profiler::report() {
    std::vector<const Section*> sorted;
    for (const Section& section : state().sections) {
        sorted.push_back(&section);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const Section* a, const Section* b) { return a->averageMs > b->averageMs; });
    std::string out = "section              avg ms   last ms    max ms  calls\n";
    char line[160];
    for (const Section* section : sorted) {
        std::snprintf(line, sizeof(line), "%-18.18s %8.3f  %8.3f  %8.3f  %5u\n", section->name,
                      section->averageMs, section->lastFrameMs, section->maxMs,
                      section->lastFrameCalls);
        out += line;
    }
    return out;
}

void Profiler::reset() {
    state().sections.clear();
    state().frames = 0;
}

void Profiler::setEnabled(bool enabled) {
    state().enabled = enabled;
}

bool Profiler::isEnabled() {
    return state().enabled;
}

} // namespace ion
