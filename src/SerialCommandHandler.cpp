#include "SerialCommandHandler.h"

SerialCommandHandler::SerialCommandHandler() : taskHandle(nullptr), running(false) {
    serialMutex = xSemaphoreCreateMutex();
}

SerialCommandHandler::~SerialCommandHandler() {
    stop();
    if (serialMutex) {
        vSemaphoreDelete(serialMutex);
    }
}

void SerialCommandHandler::begin() {
    // Just basic initialization, no task creation
    Serial.println("Serial command handler initialized");
}

void SerialCommandHandler::stop() {
    if (!running) return;
    
    running = false;
    
    if (taskHandle) {
        vTaskDelete(taskHandle);
        taskHandle = nullptr;
    }
}

void SerialCommandHandler::registerCommand(const String& command, CommandCallback callback) {
    commands[command] = callback;
}

void SerialCommandHandler::unregisterCommand(const String& command) {
    commands.erase(command);
}

void SerialCommandHandler::println(const String& message) {
    if (xSemaphoreTake(serialMutex, portMAX_DELAY)) {
        Serial.println(message);
        xSemaphoreGive(serialMutex);
    }
}

void SerialCommandHandler::printf(const char* format, ...) {
    if (xSemaphoreTake(serialMutex, portMAX_DELAY)) {
        va_list args;
        va_start(args, format);
        Serial.printf(format, args);
        va_end(args);
        xSemaphoreGive(serialMutex);
    }
}

void SerialCommandHandler::commandHandlerTask(void* parameter) {
    SerialCommandHandler* handler = static_cast<SerialCommandHandler*>(parameter);
    
    while (handler->running) {
        if (Serial.available()) {
            String command = Serial.readStringUntil('\n');
            command.trim();
            
            if (command.length() > 0) {
                handler->processCommand(command);
            }
        }
        
        vTaskDelay(10 / portTICK_PERIOD_MS); // Small delay to prevent busy waiting
    }
    
    vTaskDelete(nullptr);
}

void SerialCommandHandler::processCommand(const String& command) {
    // Parse command and arguments
    std::vector<String> parts;
    int start = 0;
    int spacePos = 0;
    
    while ((spacePos = command.indexOf(' ', start)) != -1) {
        parts.push_back(command.substring(start, spacePos));
        start = spacePos + 1;
    }
    parts.push_back(command.substring(start)); // Last part
    
    if (parts.empty()) return;
    
    String cmd = parts[0];
    parts.erase(parts.begin()); // Remove command from args
    
    // Find and execute command
    auto it = commands.find(cmd);
    if (it != commands.end()) {
        // Remove try-catch block - just execute directly
        it->second(parts); // Execute callback
    } else {
        println("Unknown command: " + cmd);
        println("Type 'help' for available commands");
    }
}

void SerialCommandHandler::showHelp() {
    println("Available commands:");
    for (const auto& cmd : commands) {
        if (cmd.first != "help") { // Don't show help in help
            println("  " + cmd.first);
        }
    }
    println("  help - Show this help");
}

void SerialCommandHandler::loop() {
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        
        if (command.length() > 0) {
            processCommand(command);
        }
    }
}