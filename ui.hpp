#pragma once
#include <vector>
#include <string>

class UI {
public:
    void begin();
    void print(const std::string& text);
    void println(const std::string& text);

    void render();

    void setStatus(const std::string& left, const std::string& right);

    std::string getInput();

private:
    std::vector<std::string> buffer;

    std::string currentLine;
    std::string inputLine;

    std::string statusLeft;
    std::string statusRight;

    int maxLines = 15;

    void wrapAndPush(const std::string& line);
};
