#include "dualsense.h"

#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <cstring>
#include <cmath>

#include <hidsdi.h>
#include <hidpi.h>
#include <setupapi.h>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

namespace trinity::hooks::dualsense
{
    namespace
    {
        constexpr USHORT kSonyVendorId       = 0x054C;
        constexpr USHORT kDualSensePid       = 0x0CE6; // PS5 DualSense Standard
        constexpr USHORT kDualSenseEdgePid   = 0x0DF2; // PS5 DualSense Edge
        constexpr USHORT kDualShock4Gen1Pid  = 0x05C4; // PS4 DualShock 4 v1
        constexpr USHORT kDualShock4Gen2Pid  = 0x09CC; // PS4 DualShock 4 v2

        std::atomic<bool> g_running{false};
        std::thread       g_workerThread;

        std::mutex        g_stateMutex;
        XINPUT_STATE      g_currentState{};
        std::atomic<bool> g_connected{false};
        char              g_deviceName[64] = "None";
        DWORD             g_packetNumber = 0;

        SHORT ScaleAxis(uint8_t raw, bool invert)
        {
            // raw: 0 (left/up), 128 (center), 255 (right/down)
            int v = static_cast<int>(raw) - 128;
            if (invert) v = -v; // Invert Y: XInput up is positive, HID raw 0 is up

            if (v > 0)
            {
                int scaled = (v * 32767) / 127;
                return static_cast<SHORT>(scaled > 32767 ? 32767 : scaled);
            }
            else if (v < 0)
            {
                int scaled = (v * 32768) / 128;
                return static_cast<SHORT>(scaled < -32768 ? -32768 : scaled);
            }
            return 0;
        }

        void ParseReport(const uint8_t* data, size_t size, USHORT pid)
        {
            if (!data || size < 9) return;

            uint8_t reportId = data[0];
            bool isDS4 = (pid == kDualShock4Gen1Pid || pid == kDualShock4Gen2Pid);

            uint8_t lsX = 128, lsY = 128, rsX = 128, rsY = 128;
            uint8_t l2 = 0, r2 = 0;
            uint8_t b1 = 0, b2 = 0;

            if (isDS4)
            {
                if (reportId == 0x11 && size >= 13)
                {
                    // DS4 Bluetooth extended
                    lsX = data[3]; lsY = data[4]; rsX = data[5]; rsY = data[6];
                    b1 = data[8]; b2 = data[9];
                    l2 = data[11]; r2 = data[12];
                }
                else
                {
                    // DS4 USB or basic BT (0x01)
                    size_t off = (reportId == 0x01) ? 1 : 0;
                    if (off + 8 < size)
                    {
                        lsX = data[off];     lsY = data[off + 1];
                        rsX = data[off + 2]; rsY = data[off + 3];
                        b1  = data[off + 4]; b2  = data[off + 5];
                        l2  = data[off + 7]; r2  = data[off + 8];
                    }
                }
            }
            else // DualSense / DualSense Edge
            {
                if (reportId == 0x31 && size >= 11)
                {
                    // DualSense Bluetooth extended report (0x31)
                    lsX = data[2]; lsY = data[3]; rsX = data[4]; rsY = data[5];
                    l2  = data[6]; r2  = data[7];
                    b1  = data[9]; b2  = data[10];
                }
                else if (reportId == 0x01 && size >= 64)
                {
                    // DualSense USB report (0x01, 64 bytes)
                    lsX = data[1]; lsY = data[2]; rsX = data[3]; rsY = data[4];
                    l2  = data[5]; r2  = data[6];
                    b1  = data[8]; b2  = data[9];
                }
                else if (reportId == 0x01 && size < 64 && size >= 9)
                {
                    // DualSense Bluetooth basic report (0x01, 10 bytes)
                    lsX = data[1]; lsY = data[2]; rsX = data[3]; rsY = data[4];
                    b1  = data[5]; b2  = data[6];
                    if (size >= 10)
                    {
                        l2 = data[8]; r2 = data[9];
                    }
                }
                else if (size >= 10)
                {
                    // Fallback / stripped report ID
                    if (size >= 64)
                    {
                        lsX = data[0]; lsY = data[1]; rsX = data[2]; rsY = data[3];
                        l2  = data[4]; r2  = data[5];
                        b1  = data[7]; b2  = data[8];
                    }
                    else
                    {
                        lsX = data[0]; lsY = data[1]; rsX = data[2]; rsY = data[3];
                        b1  = data[4]; b2  = data[5];
                        if (size >= 9)
                        {
                            l2 = data[7]; r2 = data[8];
                        }
                    }
                }
            }

            XINPUT_GAMEPAD pad{};
            pad.sThumbLX = ScaleAxis(lsX, false);
            pad.sThumbLY = ScaleAxis(lsY, true);
            pad.sThumbRX = ScaleAxis(rsX, false);
            pad.sThumbRY = ScaleAxis(rsY, true);

            // Apply light deadzone
            constexpr SHORT kDeadzone = 2500;
            if (std::abs(pad.sThumbLX) < kDeadzone && std::abs(pad.sThumbLY) < kDeadzone)
            {
                pad.sThumbLX = 0;
                pad.sThumbLY = 0;
            }
            if (std::abs(pad.sThumbRX) < kDeadzone && std::abs(pad.sThumbRY) < kDeadzone)
            {
                pad.sThumbRX = 0;
                pad.sThumbRY = 0;
            }

            pad.bLeftTrigger  = l2;
            pad.bRightTrigger = r2;

            // D-Pad decoding (Hat switch in low 4 bits)
            uint8_t dpad = b1 & 0x0F;
            switch (dpad)
            {
            case 0: pad.wButtons |= XINPUT_GAMEPAD_DPAD_UP; break;
            case 1: pad.wButtons |= (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_RIGHT); break;
            case 2: pad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT; break;
            case 3: pad.wButtons |= (XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_RIGHT); break;
            case 4: pad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN; break;
            case 5: pad.wButtons |= (XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT); break;
            case 6: pad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT; break;
            case 7: pad.wButtons |= (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT); break;
            default: break; // 8 = released / centered
            }

            // Face Buttons (High 4 bits of b1):
            // Bit 4 = Square (X)
            // Bit 5 = Cross (A)
            // Bit 6 = Circle (B)
            // Bit 7 = Triangle (Y)
            if (b1 & 0x10) pad.wButtons |= XINPUT_GAMEPAD_X;
            if (b1 & 0x20) pad.wButtons |= XINPUT_GAMEPAD_A;
            if (b1 & 0x40) pad.wButtons |= XINPUT_GAMEPAD_B;
            if (b1 & 0x80) pad.wButtons |= XINPUT_GAMEPAD_Y;

            // Shoulder, Thumbs, and Menu Buttons (b2):
            if (b2 & 0x01) pad.wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;  // L1
            if (b2 & 0x02) pad.wButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER; // R1
            if (b2 & 0x10) pad.wButtons |= XINPUT_GAMEPAD_BACK;           // Share / Create
            if (b2 & 0x20) pad.wButtons |= XINPUT_GAMEPAD_START;          // Options
            if (b2 & 0x40) pad.wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;     // L3
            if (b2 & 0x80) pad.wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;    // R3

            {
                std::lock_guard<std::mutex> lock(g_stateMutex);
                g_currentState.Gamepad = pad;
                g_currentState.dwPacketNumber = ++g_packetNumber;
            }
        }

        HANDLE FindAndOpenDevice(USHORT* outPid, bool* outIsBluetooth)
        {
            GUID hidGuid;
            HidD_GetHidGuid(&hidGuid);

            HDEVINFO devInfo = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
            if (devInfo == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

            SP_DEVICE_INTERFACE_DATA devData{};
            devData.cbSize = sizeof(devData);

            HANDLE resultHandle = INVALID_HANDLE_VALUE;

            for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &devData); ++i)
            {
                DWORD reqSize = 0;
                SetupDiGetDeviceInterfaceDetailW(devInfo, &devData, nullptr, 0, &reqSize, nullptr);
                if (reqSize == 0) continue;

                std::vector<BYTE> buf(reqSize);
                auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
                detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

                if (SetupDiGetDeviceInterfaceDetailW(devInfo, &devData, detail, reqSize, nullptr, nullptr))
                {
                    HANDLE h = CreateFileW(detail->DevicePath,
                                           GENERIC_READ | GENERIC_WRITE,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                                           nullptr,
                                           OPEN_EXISTING,
                                           FILE_FLAG_OVERLAPPED,
                                           nullptr);
                    if (h == INVALID_HANDLE_VALUE)
                    {
                        h = CreateFileW(detail->DevicePath,
                                        GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        nullptr,
                                        OPEN_EXISTING,
                                        FILE_FLAG_OVERLAPPED,
                                        nullptr);
                    }

                    if (h != INVALID_HANDLE_VALUE)
                    {
                        HIDD_ATTRIBUTES attr{};
                        attr.Size = sizeof(attr);
                        if (HidD_GetAttributes(h, &attr))
                        {
                            if (attr.VendorID == kSonyVendorId &&
                                (attr.ProductID == kDualSensePid || attr.ProductID == kDualSenseEdgePid ||
                                 attr.ProductID == kDualShock4Gen1Pid || attr.ProductID == kDualShock4Gen2Pid))
                            {
                                bool isGamepadUsage = true;
                                PHIDP_PREPARSED_DATA prep = nullptr;
                                if (HidD_GetPreparsedData(h, &prep))
                                {
                                    HIDP_CAPS caps{};
                                    if (HidP_GetCaps(prep, &caps) == HIDP_STATUS_SUCCESS)
                                    {
                                        if (caps.UsagePage == 0x01 && (caps.Usage == 0x05 || caps.Usage == 0x04))
                                            isGamepadUsage = true;
                                        else
                                            isGamepadUsage = false;
                                    }
                                    HidD_FreePreparsedData(prep);
                                }

                                if (isGamepadUsage)
                                {
                                    if (outPid) *outPid = attr.ProductID;
                                    if (outIsBluetooth)
                                    {
                                        *outIsBluetooth = (wcsstr(detail->DevicePath, L"bth") != nullptr ||
                                                           wcsstr(detail->DevicePath, L"BTH") != nullptr ||
                                                           wcsstr(detail->DevicePath, L"{00001124") != nullptr);
                                    }
                                    resultHandle = h;
                                    break;
                                }
                            }
                        }
                        CloseHandle(h);
                    }
                }
            }

            SetupDiDestroyDeviceInfoList(devInfo);
            return resultHandle;
        }

        void WorkerLoop()
        {
            uint8_t buffer[128];
            OVERLAPPED ov{};
            ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

            while (g_running.load(std::memory_order_relaxed))
            {
                USHORT pid = 0;
                bool isBT = false;
                HANDLE hDevice = FindAndOpenDevice(&pid, &isBT);

                if (hDevice == INVALID_HANDLE_VALUE)
                {
                    g_connected.store(false, std::memory_order_release);
                    strcpy_s(g_deviceName, "None");
                    for (int i = 0; i < 20 && g_running.load(std::memory_order_relaxed); ++i)
                    {
                        Sleep(50);
                    }
                    continue;
                }

                const char* model = (pid == kDualSenseEdgePid) ? "DualSense Edge" :
                                    (pid == kDualSensePid)     ? "DualSense" :
                                    (pid == kDualShock4Gen2Pid)? "DualShock 4 v2" : "DualShock 4";
                sprintf_s(g_deviceName, "%s (%s)", model, isBT ? "Bluetooth" : "USB");
                g_connected.store(true, std::memory_order_release);

                while (g_running.load(std::memory_order_relaxed))
                {
                    ResetEvent(ov.hEvent);
                    DWORD bytesRead = 0;
                    BOOL ok = ReadFile(hDevice, buffer, sizeof(buffer), &bytesRead, &ov);

                    if (!ok)
                    {
                        DWORD err = GetLastError();
                        if (err == ERROR_IO_PENDING)
                        {
                            DWORD waitRes = WaitForSingleObject(ov.hEvent, 100);
                            if (waitRes == WAIT_OBJECT_0)
                            {
                                if (GetOverlappedResult(hDevice, &ov, &bytesRead, FALSE))
                                {
                                    ok = TRUE;
                                }
                                else
                                {
                                    break;
                                }
                            }
                            else if (waitRes == WAIT_TIMEOUT)
                            {
                                CancelIo(hDevice);
                                GetOverlappedResult(hDevice, &ov, &bytesRead, TRUE);
                                continue;
                            }
                            else
                            {
                                break;
                            }
                        }
                        else
                        {
                            break;
                        }
                    }

                    if (ok && bytesRead > 0)
                    {
                        ParseReport(buffer, bytesRead, pid);
                    }
                }

                CloseHandle(hDevice);
                g_connected.store(false, std::memory_order_release);
                strcpy_s(g_deviceName, "None");
                Sleep(200);
            }

            if (ov.hEvent) CloseHandle(ov.hEvent);
        }
    }

    void Start()
    {
        if (g_running.exchange(true)) return;
        g_workerThread = std::thread(WorkerLoop);
    }

    void Stop()
    {
        if (!g_running.exchange(false)) return;
        if (g_workerThread.joinable())
            g_workerThread.join();
        g_connected.store(false, std::memory_order_release);
    }

    bool IsConnected()
    {
        return g_connected.load(std::memory_order_acquire);
    }

    bool GetState(XINPUT_STATE* outState)
    {
        if (!outState || !IsConnected()) return false;
        std::lock_guard<std::mutex> lock(g_stateMutex);
        *outState = g_currentState;
        return true;
    }

    const char* GetDeviceName()
    {
        return g_deviceName;
    }
}
