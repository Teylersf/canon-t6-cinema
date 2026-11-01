// Narrow, read-only Canon T6 DeviceInfo query through native Windows WPD.
// GPL-2.0. Based on the documented WPD flow audited in petabyt/libwpd;
// no camlib vendor operations, generic CLI, or third-party DLL is linked.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#include <PortableDeviceApi.h>
#include <PortableDevice.h>
#include <WpdMtpExtensions.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
static constexpr ULONG kGetDeviceInfo = 0x1001;
static constexpr size_t kPayloadCap = 65536;
static constexpr size_t kArrayCap = 2048;

struct Error { const char *stage; HRESULT hr; };
static void checked(HRESULT hr, const char *stage)
{
    if (FAILED(hr)) throw Error{stage, hr};
}

struct SecureBytes {
    std::vector<uint8_t> bytes;
    ~SecureBytes() { if (!bytes.empty()) SecureZeroMemory(bytes.data(), bytes.size()); }
};

struct Info {
    uint16_t standard = 0, extension_version = 0, mode = 0;
    uint32_t extension_id = 0;
    std::wstring manufacturer, model, firmware;
    std::vector<uint16_t> operations, events, properties, capture_formats, image_formats;
};

class Reader {
    const std::vector<uint8_t> &data;
    size_t pos = 0;
    void require(size_t n) const {
        if (pos > data.size() || n > data.size() - pos) throw Error{"DeviceInfo bounds", E_INVALIDARG};
    }
public:
    explicit Reader(const std::vector<uint8_t> &input) : data(input) {}
    uint16_t u16() {
        require(2);
        uint16_t value = static_cast<uint16_t>(data[pos] | (static_cast<uint16_t>(data[pos + 1]) << 8));
        pos += 2;
        return value;
    }
    uint32_t u32() {
        const uint32_t low = u16(), high = u16();
        return low | (high << 16);
    }
    std::wstring text(bool discard = false) {
        require(1);
        const uint8_t count = data[pos++];
        require(static_cast<size_t>(count) * 2);
        std::wstring result;
        for (unsigned i = 0; i < count; ++i) {
            const uint16_t value = u16();
            if (i + 1 == count) {
                if (value) throw Error{"DeviceInfo string terminator", E_INVALIDARG};
            } else if (!discard) {
                result.push_back(static_cast<wchar_t>(value));
            }
        }
        return result;
    }
    std::vector<uint16_t> array() {
        const uint32_t count = u32();
        if (count > kArrayCap) throw Error{"DeviceInfo array cap", E_INVALIDARG};
        require(static_cast<size_t>(count) * 2);
        std::vector<uint16_t> result;
        result.reserve(count);
        for (uint32_t i = 0; i < count; ++i) result.push_back(u16());
        return result;
    }
    bool finished() const { return pos == data.size(); }
};

static Info parse(const std::vector<uint8_t> &payload)
{
    if (payload.empty() || payload.size() > kPayloadCap) throw Error{"DeviceInfo size", E_INVALIDARG};
    Reader reader(payload);
    Info info;
    info.standard = reader.u16();
    info.extension_id = reader.u32();
    info.extension_version = reader.u16();
    reader.text(true); // Vendor extension description is not needed.
    info.mode = reader.u16();
    info.operations = reader.array();
    info.events = reader.array();
    info.properties = reader.array();
    info.capture_formats = reader.array();
    info.image_formats = reader.array();
    info.manufacturer = reader.text();
    info.model = reader.text();
    info.firmware = reader.text();
    reader.text(true); // Skip the serial number; never materialize or print it.
    if (!reader.finished()) throw Error{"DeviceInfo trailing bytes", E_INVALIDARG};
    return info;
}

static ComPtr<IPortableDeviceValues> values()
{
    ComPtr<IPortableDeviceValues> result;
    checked(CoCreateInstance(CLSID_PortableDeviceValues, nullptr, CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(result.GetAddressOf())), "create values");
    return result;
}

static ComPtr<IPortableDeviceValues> command(const PROPERTYKEY &key)
{
    // Only these three transport commands can be built. The only PTP opcode
    // supplied below is the compile-time constant GetDeviceInfo (0x1001).
    if (!IsEqualPropertyKey(key, WPD_COMMAND_MTP_EXT_EXECUTE_COMMAND_WITH_DATA_TO_READ) &&
        !IsEqualPropertyKey(key, WPD_COMMAND_MTP_EXT_READ_DATA) &&
        !IsEqualPropertyKey(key, WPD_COMMAND_MTP_EXT_END_DATA_TRANSFER))
        throw Error{"transport command allowlist", E_ACCESSDENIED};
    auto result = values();
    checked(result->SetGuidValue(WPD_PROPERTY_COMMON_COMMAND_CATEGORY, key.fmtid), "command category");
    checked(result->SetUnsignedIntegerValue(WPD_PROPERTY_COMMON_COMMAND_ID, key.pid), "command id");
    // Send the custom request through the documented read-access IOCTL.
    // Without this option WPD may reject SendCommand on a GENERIC_READ handle.
    checked(result->SetUnsignedIntegerValue(WPD_API_OPTION_IOCTL_ACCESS, FILE_READ_ACCESS), "read-only command IOCTL");
    return result;
}

static ComPtr<IPortableDeviceValues> send(IPortableDevice *device, IPortableDeviceValues *request)
{
    ComPtr<IPortableDeviceValues> result;
    checked(device->SendCommand(0, request, result.GetAddressOf()), "WPD SendCommand");
    if (!result) throw Error{"empty WPD result", E_FAIL};
    HRESULT status = E_FAIL;
    checked(result->GetErrorValue(WPD_PROPERTY_COMMON_HRESULT, &status), "WPD result status");
    checked(status, "WPD command result");
    return result;
}

struct DeviceIds {
    std::vector<PWSTR> ids;
    ~DeviceIds() { for (auto id : ids) CoTaskMemFree(id); }
};

static std::wstring find_t6()
{
    ComPtr<IPortableDeviceManager> manager;
    checked(CoCreateInstance(CLSID_PortableDeviceManager, nullptr, CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(manager.GetAddressOf())), "create device manager");
    DWORD count = 0;
    checked(manager->GetDevices(nullptr, &count), "count WPD devices");
    if (count > 128) throw Error{"WPD device count cap", E_INVALIDARG};
    DeviceIds list;
    list.ids.resize(count, nullptr);
    if (count) checked(manager->GetDevices(list.ids.data(), &count), "read WPD device ids");
    std::wstring selected;
    size_t matches = 0;
    for (auto id : list.ids) {
        if (!id) continue;
        std::wstring upper(id);
        std::transform(upper.begin(), upper.end(), upper.begin(),
            [](wchar_t ch) { return static_cast<wchar_t>(towupper(ch)); });
        if (upper.find(L"VID_04A9&PID_32B4") != std::wstring::npos) {
            selected = id;
            ++matches;
        }
    }
    if (matches != 1) throw Error{"expected exactly one Canon T6 VID04A9 PID32B4", HRESULT_FROM_WIN32(ERROR_NOT_FOUND)};
    return selected; // Never log the PnP id, which can contain a serial number.
}

static void finish(IPortableDevice *device, const std::wstring &context)
{
    auto request = command(WPD_COMMAND_MTP_EXT_END_DATA_TRANSFER);
    checked(request->SetStringValue(WPD_PROPERTY_MTP_EXT_TRANSFER_CONTEXT, context.c_str()), "finish context");
    auto result = send(device, request.Get());
    ULONG response = 0;
    checked(result->GetUnsignedIntegerValue(WPD_PROPERTY_MTP_EXT_RESPONSE_CODE, &response), "PTP response code");
    if (response != 0x2001) throw Error{"GetDeviceInfo PTP response not OK", E_FAIL};
}

static Info query()
{
    auto client = values();
    checked(client->SetStringValue(WPD_CLIENT_NAME, L"T6 read-only DeviceInfo"), "client name");
    checked(client->SetUnsignedIntegerValue(WPD_CLIENT_MAJOR_VERSION, 1), "client version");
    checked(client->SetUnsignedIntegerValue(WPD_CLIENT_MINOR_VERSION, 0), "client minor");
    checked(client->SetUnsignedIntegerValue(WPD_CLIENT_REVISION, 0), "client revision");
    // Both the WPD handle and every command IOCTL request read access only.
    // No broader-access mode or automatic permission fallback is available.
    checked(client->SetUnsignedIntegerValue(WPD_CLIENT_DESIRED_ACCESS, GENERIC_READ), "read-only WPD transport access");
    const auto id = find_t6();
    ComPtr<IPortableDevice> device;
    checked(CoCreateInstance(CLSID_PortableDeviceFTM, nullptr, CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(device.GetAddressOf())), "create WPD device");
    checked(device->Open(id.c_str(), client.Get()), "open T6 WPD transport");
    std::wstring context;
    bool active = false;
    try {
        auto request = command(WPD_COMMAND_MTP_EXT_EXECUTE_COMMAND_WITH_DATA_TO_READ);
        checked(request->SetUnsignedIntegerValue(WPD_PROPERTY_MTP_EXT_OPERATION_CODE, kGetDeviceInfo), "GetDeviceInfo opcode");
        ComPtr<IPortableDevicePropVariantCollection> parameters;
        checked(CoCreateInstance(CLSID_PortableDevicePropVariantCollection, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(parameters.GetAddressOf())), "empty PTP params");
        checked(request->SetIPortableDevicePropVariantCollectionValue(
                    WPD_PROPERTY_MTP_EXT_OPERATION_PARAMS, parameters.Get()), "GetDeviceInfo params");
        auto response = send(device.Get(), request.Get());
        PWSTR raw_context = nullptr;
        checked(response->GetStringValue(WPD_PROPERTY_MTP_EXT_TRANSFER_CONTEXT, &raw_context), "read transfer context");
        if (!raw_context) throw Error{"empty transfer context", E_FAIL};
        context = raw_context;
        CoTaskMemFree(raw_context);
        active = true;
        ULONGLONG total = 0;
        checked(response->GetUnsignedLargeIntegerValue(WPD_PROPERTY_MTP_EXT_TRANSFER_TOTAL_DATA_SIZE, &total), "read transfer size");
        if (!total || total > kPayloadCap) throw Error{"DeviceInfo transfer cap", E_INVALIDARG};
        SecureBytes payload;
        payload.bytes.reserve(static_cast<size_t>(total));
        while (payload.bytes.size() < total) {
            const ULONG want = static_cast<ULONG>(std::min<ULONGLONG>(4096, total - payload.bytes.size()));
            auto read = command(WPD_COMMAND_MTP_EXT_READ_DATA);
            checked(read->SetStringValue(WPD_PROPERTY_MTP_EXT_TRANSFER_CONTEXT, context.c_str()), "read context");
            checked(read->SetUnsignedIntegerValue(WPD_PROPERTY_MTP_EXT_TRANSFER_NUM_BYTES_TO_READ, want), "read byte count");
            uint8_t scratch[4096] = {};
            checked(read->SetBufferValue(WPD_PROPERTY_MTP_EXT_TRANSFER_DATA, scratch, want), "receive buffer");
            auto received = send(device.Get(), read.Get());
            BYTE *bytes = nullptr;
            DWORD length = 0;
            checked(received->GetBufferValue(WPD_PROPERTY_MTP_EXT_TRANSFER_DATA, &bytes, &length), "receive DeviceInfo");
            ULONG actual = 0;
            const HRESULT actual_hr = received->GetUnsignedIntegerValue(WPD_PROPERTY_MTP_EXT_TRANSFER_NUM_BYTES_READ, &actual);
            if (FAILED(actual_hr) || !bytes || !length || length > want || actual != length) {
                if (bytes) { SecureZeroMemory(bytes, length); CoTaskMemFree(bytes); }
                throw Error{"DeviceInfo read bounds", E_INVALIDARG};
            }
            payload.bytes.insert(payload.bytes.end(), bytes, bytes + length);
            SecureZeroMemory(bytes, length);
            CoTaskMemFree(bytes);
        }
        finish(device.Get(), context);
        active = false;
        auto info = parse(payload.bytes);
        checked(device->Close(), "close T6");
        return info;
    } catch (...) {
        if (active) { try { finish(device.Get(), context); } catch (...) {} }
        device->Close();
        throw;
    }
}

static std::string json_string(const std::wstring &value)
{
    std::string result = "\"";
    for (wchar_t ch : value) {
        if (ch == L'"' || ch == L'\\') { result += '\\'; result += static_cast<char>(ch); }
        else if (ch >= 32 && ch <= 126) result += static_cast<char>(ch);
        else {
            char escape[8];
            sprintf_s(escape, "\\u%04x", static_cast<unsigned>(ch) & 0xffffu);
            result += escape;
        }
    }
    return result + '"';
}

static void print_array(const char *name, const std::vector<uint16_t> &values)
{
    std::printf("  \"%s\": [", name);
    for (size_t i = 0; i < values.size(); ++i) std::printf("%s\"0x%04x\"", i ? ", " : "", values[i]);
    std::printf("]");
}

static void print_info(const Info &info)
{
    std::printf("{\n  \"manufacturer\": %s,\n  \"model\": %s,\n  \"firmware\": %s,\n",
        json_string(info.manufacturer).c_str(), json_string(info.model).c_str(), json_string(info.firmware).c_str());
    std::printf("  \"serial_number\": \"[redacted]\",\n  \"ptp_opcode_sent\": \"0x1001\",\n");
    std::printf("  \"wpd_transport_access\": \"GENERIC_READ\",\n");
    std::printf("  \"wpd_ioctl_access\": \"FILE_READ_ACCESS\",\n");
    std::printf("  \"standard_version\": %u,\n  \"vendor_extension_id\": %u,\n", info.standard, info.extension_id);
    print_array("operations_supported", info.operations); std::printf(",\n");
    print_array("events_supported", info.events); std::printf(",\n");
    print_array("device_properties_supported", info.properties); std::printf(",\n");
    print_array("capture_formats", info.capture_formats); std::printf(",\n");
    print_array("image_formats", info.image_formats); std::printf("\n}\n");
}

static void append16(std::vector<uint8_t> &bytes, uint16_t value)
{
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
}
static void append32(std::vector<uint8_t> &bytes, uint32_t value)
{
    append16(bytes, static_cast<uint16_t>(value)); append16(bytes, static_cast<uint16_t>(value >> 16));
}
static void append_text(std::vector<uint8_t> &bytes, const wchar_t *text)
{
    bytes.push_back(static_cast<uint8_t>(wcslen(text) + 1));
    while (*text) append16(bytes, static_cast<uint16_t>(*text++));
    append16(bytes, 0);
}

static int self_test()
{
    // Synthetic parser tests only. Never initialize COM or enumerate devices.
    std::vector<uint8_t> sample;
    append16(sample, 100); append32(sample, 6); append16(sample, 100);
    append_text(sample, L"Canon extension"); append16(sample, 0);
    for (int i = 0; i < 5; ++i) { append32(sample, 1); append16(sample, static_cast<uint16_t>(0x1001 + i)); }
    append_text(sample, L"Canon Inc."); append_text(sample, L"Canon EOS Rebel T6");
    append_text(sample, L"3-1.1.0"); append_text(sample, L"SYNTHETIC_SERIAL_MUST_NOT_APPEAR");
    const auto info = parse(sample);
    if (info.firmware != L"3-1.1.0" || info.operations.size() != 1 || info.operations[0] != 0x1001) return 1;
    for (size_t i = 0; i < sample.size(); ++i) {
        std::vector<uint8_t> shortened(sample.begin(), sample.begin() + static_cast<ptrdiff_t>(i));
        bool rejected = false;
        try { (void)parse(shortened); } catch (const Error &) { rejected = true; }
        if (!rejected) return 2;
    }
    auto bad = sample; bad.back() = 1;
    try { (void)parse(bad); return 3; } catch (const Error &) {}
    bad = sample; bad.push_back(1);
    try { (void)parse(bad); return 4; } catch (const Error &) {}
    std::printf("Synthetic DeviceInfo parser tests passed (%zu truncations); no camera access.\n", sample.size());
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !std::strcmp(argv[1], "--self-test")) return self_test();
    if (argc != 2 || std::strcmp(argv[1], "--query-device-info")) {
        std::puts("T6 read-only camera-operation DeviceInfo client. Explicit query flag required.\n"
                  "--self-test: synthetic parser tests only.\n"
                  "--query-device-info: GENERIC_READ handle + FILE_READ_ACCESS IOCTL; PTP 0x1001 only.\n"
                  "Selects exactly one VID04A9/PID32B4 camera. No broader-access mode or fallback.\n"
                  "Serial numbers, PnP ids and raw payloads are never printed or saved.");
        return argc == 1 ? 0 : 2;
    }
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) { std::fprintf(stderr, "COM initialization failed: 0x%08lx\n", static_cast<unsigned long>(initialized)); return 1; }
    int result = 0;
    try { print_info(query()); }
    catch (const Error &error) {
        std::fprintf(stderr, "%s failed: 0x%08lx\n", error.stage, static_cast<unsigned long>(error.hr));
        result = 1;
    } catch (...) { std::fputs("DeviceInfo query failed; private payload omitted.\n", stderr); result = 1; }
    CoUninitialize();
    return result;
}
