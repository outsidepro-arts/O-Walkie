#include <iostream>

extern int run_protocol_tests();
extern int run_json_tests();
extern int run_json_wire_tests();
extern int run_signal_tests();
extern int run_udp_tests();
extern int run_link_signal_tests();
extern int run_session_info_tests();

int main() {
    int failed = 0;
    failed += run_protocol_tests();
    failed += run_json_tests();
    failed += run_json_wire_tests();
    failed += run_signal_tests();
    failed += run_udp_tests();
    failed += run_link_signal_tests();
    failed += run_session_info_tests();
    if (failed == 0) {
        std::cout << "All tests passed.\n";
        return 0;
    }
    std::cerr << failed << " test(s) failed.\n";
    return 1;
}
