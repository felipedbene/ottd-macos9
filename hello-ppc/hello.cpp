#include <string>
#include <vector>
int main() {
    std::vector<std::string> v{"OpenTTD", "on", "Mac", "OS", "9"};
    std::string s;
    for (auto& w : v) { s += w; s += ' '; }
    return (int)s.size() & 0;   // exercises libstdc++ string/vector on PPC
}
