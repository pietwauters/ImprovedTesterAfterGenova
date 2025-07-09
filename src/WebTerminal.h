#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <functional>
#include <vector>

class WebTerminal {
public:
    WebTerminal(AsyncWebServer& server);

    void begin();
    void loop();
    void send(const String& msg);
    void printf(const char* format, ...);
    void handleCommand(const String& input);

    using CommandCallback = std::function<void(const std::vector<String>& args)>;
    void registerCommand(const String& name, CommandCallback callback);

private:
    AsyncWebSocket ws;
    AsyncWebServer& server;

    struct Command {
        String name;
        CommandCallback callback;
    };

    std::vector<Command> commands;
};
