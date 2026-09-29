#include "support/FakeSystemInspector.h"

#include <utility>

namespace workpane::tests {

FakeSystemInspector::FakeSystemInspector(std::shared_ptr<SystemRecord> record) : m_record(std::move(record)) {}

Result<nlohmann::json> FakeSystemInspector::inspect() {
    ++m_record->inspections;

    if (m_record->inspectionFails) {
        return Result<nlohmann::json>::failure({"system_inspection_failed", "The hardware could not be read", "test"});
    }

    return Result<nlohmann::json>::success(m_record->snapshot.is_null() ? sample() : m_record->snapshot);
}

nlohmann::json FakeSystemInspector::sample() {
    const nlohmann::json core{{"id", 0}, {"l1Data", 65536}, {"l1Instruction", 131072}, {"l2", 4194304}, {"l3", 0}, {"maximumFrequency", 3200000000LL}, {"smt", false}};
    const nlohmann::json processor{{"vendor", "Test Vendor"}, {"model", "Test Processor"}, {"physicalCores", 8}, {"logicalCores", 8}, {"cores", {core}}, {"flags", {"AES", "SHA256"}}};
    const nlohmann::json disk{{"vendor", "Test"}, {"model", "Test SSD"}, {"serial", "S1"}, {"interface", "PCI-Express"}, {"size", 512110190592LL}, {"volumes", {{{"mountPoint", "/"}, {"free", 123456789012LL}}}}};
    const nlohmann::json battery{{"vendor", "Test"}, {"model", "Test Battery"}, {"serial", "B1"}, {"technology", ""}, {"state", "charging"}, {"capacity", 0.8}};
    const nlohmann::json adapter{{"vendor", "Test"}, {"name", "Test Graphics"}, {"driver", ""}, {"vendorId", "0x106B"}, {"deviceId", "0x0001"}, {"dedicatedMemory", 0}, {"sharedMemory", 0}, {"frequency", 0}, {"cores", 16}};
    const nlohmann::json interface{{"index", 4}, {"description", "en0"}, {"mac", "00:11:22:33:44:55"}, {"ipv4", "192.168.0.10"}, {"ipv6", "fe80::1"}};
    return {{"os", {{"hostName", "test-host"}, {"name", "Test OS"}, {"version", "1.0 (1A1)"}, {"kernel", "Test 1.0"}, {"architectureBits", 64}, {"byteOrder", "little-endian"}}}, {"processors", {processor}}, {"processorUsage", {{"utilization", 0.25}, {"threads", {{{"utilization", 0.25}, {"frequency", 0}}}}}}, {"memory", {{"total", 34359738368LL}, {"free", 4294967296LL}, {"available", 17179869184LL}, {"modules", nlohmann::json::array()}}}, {"graphics", {adapter}}, {"mainboard", {{"vendor", "Test Board Vendor"}, {"name", "Test Board"}, {"version", "1"}, {"serial", "M1"}}}, {"disks", {disk}}, {"batteries", {battery}}, {"networkInterfaces", {interface}}};
}

} // namespace workpane::tests
