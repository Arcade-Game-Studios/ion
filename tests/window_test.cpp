#include <ion/core/Application.hpp>
#include <ion/core/Version.hpp>

#include <cstdio>

class TestApp : public ion::Application {
public:
    void initialize() override {
        std::printf("Ion Engine v%s window test\n", ion::VERSION_STRING);
    }

    void update(float deltaTime) override {
        (void)deltaTime;
    }

    void render() override {
    }
};

int main() {
    TestApp app;
    app.run();
    return 0;
}
