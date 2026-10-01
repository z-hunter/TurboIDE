#include "../src/gdb_mi.h"

int main() {
    const std::string reply = R"(7^done,variables=[{name="node",value="{child={leaf=1},text=\"}\"}",type="struct node"},{name="message",value="say \"hi\" \\ ok\nnext",type="char *"}])";
    size_t pos = reply.find("{name=\"");
    if (pos == std::string::npos) return 1;
    const size_t firstEnd = gdbmi::recordEnd(reply, pos);
    if (firstEnd == std::string::npos) return 2;
    const auto first = reply.substr(pos, firstEnd - pos);
    if (gdbmi::stringAttribute(first, "name") != "node") return 3;
    if (gdbmi::stringAttribute(first, "value") != "{child={leaf=1},text=\"}\"}") return 4;

    pos = reply.find("{name=\"", firstEnd);
    if (pos == std::string::npos) return 5;
    const size_t secondEnd = gdbmi::recordEnd(reply, pos);
    if (secondEnd == std::string::npos) return 6;
    const auto second = reply.substr(pos, secondEnd - pos);
    if (gdbmi::stringAttribute(second, "value") != "say \"hi\" \\ ok\nnext") return 7;
}
