// Build 26734796 post-quant adapters, native order and inventory receipts.
#include "wave6_repair_cases.hpp"
#include "wave6_recrew_cases.hpp"
#include "wave6_scavenge_cases.hpp"

#include <cstdio>

int main() {
    goh::test::wave6_repair::run();
    goh::test::wave6_recrew::run();
    goh::test::wave6_scavenge::run();
    std::puts("Wave 6 repair, recrew and scavenging fixtures passed.");
}
