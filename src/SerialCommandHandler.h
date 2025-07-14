#ifndef SERIAL_COMMAND_HANDLER_H
#define SERIAL_COMMAND_HANDLER_H

#include <Arduino.h>
#include <functional>
#include <map>
#include <vector>

class SerialCommandHandler {
public:
    // Callback function type for commands
    typedef std::function<void(const std::vector<String>&)> CommandCallback;
    
    SerialCommandHandler();
    ~SerialCommandHandler();
    
    // Start the command handler thread
    void begin();
    
    // Stop the command handler thread
    void stop();
    
    // Register a command with a callback
    void registerCommand(const String& command, CommandCallback callback);
    
    // Remove a command
    void unregisterCommand(const String& command);
    
    // Print a message to serial (thread-safe)
    void println(const String& message);
    void printf(const char* format, ...);
    
    void loop();
    
private:
    static void commandHandlerTask(void* parameter);
    void processCommand(const String& command);
    void showHelp();
    
    std::map<String, CommandCallback> commands;
    TaskHandle_t taskHandle;
    bool running;
    SemaphoreHandle_t serialMutex;
};

#endif