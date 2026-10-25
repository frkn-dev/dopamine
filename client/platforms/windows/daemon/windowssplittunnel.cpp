/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "windowssplittunnel.h"

#include <qassert.h>

#include <memory>

#include "../windowscommons.h"
#include "../windowsservicemanager.h"
#include "logger.h"
#include "platforms/windows/daemon/windowsfirewall.h"
#include "platforms/windows/daemon/windowssplittunnel.h"
#include "platforms/windows/windowsutils.h"
#include "windowsfirewall.h"

#pragma comment(lib, "iphlpapi.lib")

#define PSAPI_VERSION 2
#include <Windows.h>
#include <iphlpapi.h>
#include <psapi.h>

#include <QCoreApplication>
#include <QFileInfo>
#include <QNetworkInterface>
#include <QScopeGuard>
#include <QUrl>

#pragma region

// Driver Configuration structures
using CONFIGURATION_ENTRY = struct {
  // Offset into buffer region that follows all entries.
  // The image name uses the device path.
  SIZE_T ImageNameOffset;
  // Length of the String
  USHORT ImageNameLength;
};

using CONFIGURATION_HEADER = struct {
  // Number of entries immediately following the header.
  SIZE_T NumEntries;

  // Total byte length: header + entries + string buffer.
  SIZE_T TotalLength;
};

// Used to Configure Which IP is network/vpn
using IP_ADDRESSES_CONFIG = struct {
  IN_ADDR TunnelIpv4;
  IN_ADDR InternetIpv4;

  IN6_ADDR TunnelIpv6;
  IN6_ADDR InternetIpv6;
};

// Used to Define Which Processes are alive on activation
using PROCESS_DISCOVERY_HEADER = struct {
  SIZE_T NumEntries;
  SIZE_T TotalLength;
};

using PROCESS_DISCOVERY_ENTRY = struct {
  HANDLE ProcessId;
  HANDLE ParentProcessId;

  SIZE_T ImageNameOffset;
  USHORT ImageNameLength;
};

using ProcessInfo = struct {
  DWORD ProcessId;
  DWORD ParentProcessId;
  FILETIME CreationTime;
  std::wstring DevicePath;
};

#ifndef CTL_CODE

#  define FILE_ANY_ACCESS 0x0000

#  define METHOD_BUFFERED 0
#  define METHOD_IN_DIRECT 1
#  define METHOD_NEITHER 3

#  define CTL_CODE(DeviceType, Function, Method, Access) \
    (((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))
#endif

// Known ControlCodes
#define IOCTL_INITIALIZE CTL_CODE(0x8000, 1, METHOD_NEITHER, FILE_ANY_ACCESS)

#define IOCTL_DEQUEUE_EVENT \
  CTL_CODE(0x8000, 2, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_REGISTER_PROCESSES \
  CTL_CODE(0x8000, 3, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_REGISTER_IP_ADDRESSES \
  CTL_CODE(0x8000, 4, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_GET_IP_ADDRESSES \
  CTL_CODE(0x8000, 5, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_SET_CONFIGURATION \
  CTL_CODE(0x8000, 6, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_GET_CONFIGURATION \
  CTL_CODE(0x8000, 7, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_CLEAR_CONFIGURATION \
  CTL_CODE(0x8000, 8, METHOD_NEITHER, FILE_ANY_ACCESS)

#define IOCTL_GET_STATE CTL_CODE(0x8000, 9, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_QUERY_PROCESS \
  CTL_CODE(0x8000, 10, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_ST_RESET CTL_CODE(0x8000, 11, METHOD_NEITHER, FILE_ANY_ACCESS)

constexpr static const auto DRIVER_SYMLINK = L"\\\\.\\MULLVADSPLITTUNNEL";
constexpr static const auto DRIVER_FILENAME = "mullvad-split-tunnel.sys";
constexpr static const auto DRIVER_SERVICE_NAME = L"DopamineSplitTunnel";
// pre-rebrand registration — clean it up on sight
constexpr static const auto LEGACY_DRIVER_SERVICE_NAME = L"AmneziaVPNSplitTunnel";
constexpr static const auto MV_SERVICE_NAME = L"MullvadVPN";

#pragma endregion

namespace {
Logger logger("WindowsSplitTunnel");

QString ipv4ToString(const IN_ADDR& address) {
  char buffer[INET_ADDRSTRLEN] = {0};
  InetNtopA(AF_INET, &address, buffer, sizeof(buffer));
  return QString::fromLatin1(buffer);
}

QString ipv6ToString(const IN6_ADDR& address) {
  char buffer[INET6_ADDRSTRLEN] = {0};
  InetNtopA(AF_INET6, &address, buffer, sizeof(buffer));
  return QString::fromLatin1(buffer);
}

// IPv4 interface index of an adapter, or 0 when it has no IPv4 address.
//
// IP_ADAPTER_ADDRESSES only carries the *IPv6* index (IfIndex / Ipv6IfIndex) -
// there is no Ipv4IfIndex member in the struct at all. The IPv4 index lives in
// the IP address table, keyed by the adapter's IPv4 address, and the two index
// namespaces are not interchangeable: GetBestRoute()/MIB_IPFORWARDROW report the
// IPv4 index while Qt reports the IPv6 one. On a machine where the adapter has
// IPv6 disabled they differ, and handing the driver the wrong one binds every
// excluded app to an address that routes nowhere (Amnezia issue #3306).
ULONG ipv4IfIndexFor(const IN_ADDR& ipv4) {
  if (ipv4.S_un.S_addr == 0) {
    return 0;
  }
  DWORD size = 0;
  if (GetIpAddrTable(nullptr, &size, FALSE) != ERROR_INSUFFICIENT_BUFFER) {
    return 0;
  }
  std::vector<BYTE> buffer(size);
  auto table = reinterpret_cast<MIB_IPADDRTABLE*>(buffer.data());
  if (GetIpAddrTable(table, &size, FALSE) != NO_ERROR) {
    return 0;
  }
  for (DWORD i = 0; i < table->dwNumEntries; ++i) {
    if (table->table[i].dwAddr == ipv4.S_un.S_addr) {
      return table->table[i].dwIndex;
    }
  }
  return 0;
}

// Look up the addresses of the adapter identified by `index`.
//
// Never resolve the index through QNetworkInterface: the index spaces differ.
// GetBestRoute()/MIB_IPFORWARDROW::dwForwardIfIndex use the **IPv4** interface
// index, while Qt (and IP_ADAPTER_ADDRESSES::IfIndex) use the IPv6 one.
bool collectAdapterAddresses(IP_ADAPTER_ADDRESSES* adapter, IN_ADDR* out_ipv4,
                             IN6_ADDR* out_ipv6) {
  std::memset(out_ipv4, 0, sizeof(IN_ADDR));
  std::memset(out_ipv6, 0, sizeof(IN6_ADDR));

  bool haveIpv4 = false;
  bool haveGlobalIpv6 = false;
  for (auto unicast = adapter->FirstUnicastAddress; unicast != nullptr;
       unicast = unicast->Next) {
    if (unicast->Address.lpSockaddr == nullptr) {
      continue;
    }
    if (unicast->Address.lpSockaddr->sa_family == AF_INET) {
      if (!haveIpv4) {
        *out_ipv4 = reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr)->sin_addr;
        haveIpv4 = true;
      }
    } else if (unicast->Address.lpSockaddr->sa_family == AF_INET6 && !haveGlobalIpv6) {
      const auto candidate =
          reinterpret_cast<sockaddr_in6*>(unicast->Address.lpSockaddr)->sin6_addr;
      // A link-local (fe80::/10) address is not an internet address: the driver
      // would treat its presence as "IPv6 internet available".
      if (IN6_IS_ADDR_LINKLOCAL(&candidate)) {
        continue;
      }
      *out_ipv6 = candidate;
      haveGlobalIpv6 = true;
    }
  }
  return haveIpv4;
}

// One line per adapter actually used, with both index namespaces - that is what
// makes a "wrong adapter" report diagnosable from the service log.
void logAdapter(IP_ADAPTER_ADDRESSES* adapter) {
  IN_ADDR ipv4{};
  IN6_ADDR ipv6{};
  collectAdapterAddresses(adapter, &ipv4, &ipv6);
  logger.debug() << "Adapter" << QString::fromWCharArray(adapter->FriendlyName)
                 << "ifIndex:" << adapter->IfIndex
                 << "ipv6IfIndex:" << adapter->Ipv6IfIndex
                 << "ipv4IfIndex:" << ipv4IfIndexFor(ipv4)
                 << "ipv4:" << ipv4ToString(ipv4);
}

class AdapterEnumerator {
 public:
  AdapterEnumerator() {
    const ULONG flags =
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG bufferSize = 15000;
    m_buffer.resize(bufferSize);
    m_addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(m_buffer.data());
    auto result = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, m_addresses, &bufferSize);
    if (result == ERROR_BUFFER_OVERFLOW) {
      m_buffer.resize(bufferSize);
      m_addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(m_buffer.data());
      result = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, m_addresses, &bufferSize);
    }
    if (result != NO_ERROR) {
      logger.warning() << "GetAdaptersAddresses failed:" << result;
      m_addresses = nullptr;
    }
  }

  // Matches both index namespaces: the caller may hand us the IPv4 index (route
  // table) or the IPv6 one (Qt).
  bool byIndex(int index, IN_ADDR* out_ipv4, IN6_ADDR* out_ipv6) {
    const auto wanted = static_cast<ULONG>(index);
    for (auto adapter = m_addresses; adapter != nullptr; adapter = adapter->Next) {
      if (adapter->IfIndex == wanted || adapter->Ipv6IfIndex == wanted) {
        logAdapter(adapter);
        return collectAdapterAddresses(adapter, out_ipv4, out_ipv6);
      }
    }
    // Not an IPv6-space index: compare against each adapter's IPv4 index, which
    // has to be read from the IP address table (see ipv4IfIndexFor).
    for (auto adapter = m_addresses; adapter != nullptr; adapter = adapter->Next) {
      IN_ADDR ipv4{};
      IN6_ADDR ipv6{};
      if (!collectAdapterAddresses(adapter, &ipv4, &ipv6)) {
        continue;
      }
      if (ipv4IfIndexFor(ipv4) == wanted) {
        logAdapter(adapter);
        *out_ipv4 = ipv4;
        *out_ipv6 = ipv6;
        return true;
      }
    }
    return false;
  }

  // Used for the tunnel adapter: its IPv4-only nature can leave the Qt index at
  // 0, but the adapter is always there under its friendly name.
  bool byName(const QString& friendlyName, IN_ADDR* out_ipv4, IN6_ADDR* out_ipv6) {
    for (auto adapter = m_addresses; adapter != nullptr; adapter = adapter->Next) {
      if (QString::fromWCharArray(adapter->FriendlyName)
              .compare(friendlyName, Qt::CaseInsensitive)
          == 0) {
        return collectAdapterAddresses(adapter, out_ipv4, out_ipv6);
      }
    }
    return false;
  }

 private:
  std::vector<uint8_t> m_buffer;
  IP_ADAPTER_ADDRESSES* m_addresses = nullptr;
};

ProcessInfo getProcessInfo(HANDLE process, const PROCESSENTRY32W& processMeta) {
  ProcessInfo pi;
  pi.ParentProcessId = processMeta.th32ParentProcessID;
  pi.ProcessId = processMeta.th32ProcessID;
  pi.CreationTime = {0, 0};
  pi.DevicePath = L"";

  FILETIME creationTime, null_time;
  auto ok = GetProcessTimes(process, &creationTime, &null_time, &null_time,
                            &null_time);
  if (ok) {
    pi.CreationTime = creationTime;
  }
  wchar_t imagepath[MAX_PATH + 1];
  if (K32GetProcessImageFileNameW(
          process, imagepath, sizeof(imagepath) / sizeof(*imagepath)) != 0) {
    pi.DevicePath = imagepath;
  }
  return pi;
}

}  // namespace

std::unique_ptr<WindowsSplitTunnel> WindowsSplitTunnel::create(
    WindowsFirewall* fw) {
  if (fw == nullptr) {
    // Pre-Condition:
    // Make sure the Windows Firewall has created the sublayer
    // otherwise the driver will fail to initialize
    logger.error() << "Failed to did not pass a WindowsFirewall obj"
                   << "The Driver cannot work with the sublayer not created";
    return nullptr;
  }
  // rebrand: drop the stale AmneziaVPNSplitTunnel registration before the
  // conflict check below mistakes our own driver for a foreign one
  removeLegacyDriverService();
  // 00: Check if we conflict with mullvad, if so.
  if (detectConflict()) {
    logger.error() << "Conflict detected, abort Split-Tunnel init.";
    return nullptr;
  }
  // 01: Check if the driver is installed, if not do so.
  if (!isInstalled()) {
    logger.debug() << "Driver is not Installed, doing so";
    auto handle = installDriver();
    if (handle == INVALID_HANDLE_VALUE) {
      WindowsUtils::windowsLog("Failed to install Driver");
      return nullptr;
    }
    logger.debug() << "Driver installed";
    CloseServiceHandle(handle);
  } else {
    logger.debug() << "Driver was installed";
  }
  // 02: Now check if the service is running
  auto driver_manager =
      WindowsServiceManager::open(QString::fromWCharArray(DRIVER_SERVICE_NAME));
  if (Q_UNLIKELY(driver_manager == nullptr)) {
    // Let's be fair if we end up here,
    // after checking it exists and installing it,
    // this is super unlikeley
    Q_ASSERT(false);
    logger.error()
        << "WindowsServiceManager was unable fo find Split Tunnel service?";
    return nullptr;
  }
  if (!driver_manager->isRunning()) {
    logger.debug() << "Driver is not running, starting it";
    // Start the service
    if (!driver_manager->startService()) {
      // The driver image may already be loaded (started through the legacy
      // pre-rebrand service record, or an earlier install): StartService then
      // fails even though the device is perfectly usable. Don't bail here —
      // the symlink open below is the real check.
      logger.warning() << "Split Tunnel service start failed, probing the device directly";
    };
  }
  // 03: Open the Driver Symlink
  //
  // The device is opened for exclusive use unless the caller says otherwise: a
  // handle taken with dwShareMode=0 makes every later CreateFileW on
  // \\.\DopamineSplitTunnel fail with ERROR_ACCESS_DENIED, even if the second
  // caller passes FILE_SHARE_* (verified on hardware 2026-10-23). The daemon is
  // restarted on every activation while the previous instance may still hold its
  // handle, so the exclusive open turned a restart into "split tunnel never
  // engages". Always share.
  auto driverFile =
      CreateFileW(DRIVER_SYMLINK, GENERIC_READ | GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                  nullptr);
  if (driverFile == INVALID_HANDLE_VALUE) {
    logger.error() << "Failed to open the driver device, err=" << GetLastError();
    WindowsUtils::windowsLog("Failed to open Driver: ");
    // Only once, if the opening did not work. Try to reboot it. #
    logger.info()
        << "Failed to open driver, attempting only once to reboot driver";
    if (!driver_manager->stopService()) {
      logger.error() << "Unable stop driver";
      return nullptr;
    };
    logger.info() << "Stopped driver, starting it again.";
    if (!driver_manager->startService()) {
      logger.error() << "Unable start driver";
      return nullptr;
    };
    logger.info() << "Opening again.";
    driverFile = CreateFileW(DRIVER_SYMLINK, GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, 0, nullptr);
    if (driverFile == INVALID_HANDLE_VALUE) {
      logger.error() << "Opening Failed again, sorry! err=" << GetLastError();
      return nullptr;
    }
  }
  if (!initDriver(driverFile)) {
    logger.error() << "Failed to init driver";
    return nullptr;
  }
  // We're ready to talk to the driver, it's alive and setup.
  return std::make_unique<WindowsSplitTunnel>(driverFile);
}

bool WindowsSplitTunnel::initDriver(HANDLE driverIO) {
  // We need to now check the state and init it, if required
  auto state = getState(driverIO);
  if (state == STATE_UNKNOWN) {
    logger.debug() << "Cannot check if driver is initialized";
    return false;
  }
  if (state >= STATE_INITIALIZED) {
    logger.debug() << "Driver already initialized: " << state;
    // Reset Driver as it has wfp handles probably >:(
    resetDriver(driverIO);

    auto newState = getState(driverIO);
    logger.debug() << "New state after reset:" << newState;
    if (newState >= STATE_INITIALIZED) {
      logger.debug() << "Reset unsuccesfull";
      return false;
    }
  }

  DWORD bytesReturned;
  auto ok = DeviceIoControl(driverIO, IOCTL_INITIALIZE, nullptr, 0, nullptr, 0,
                            &bytesReturned, nullptr);
  if (!ok) {
    auto err = GetLastError();
    logger.error() << "Driver init failed err -" << err;
    logger.error() << "State:" << getState(driverIO);

    return false;
  }
  logger.debug() << "Driver initialized" << getState(driverIO);
  return true;
}

WindowsSplitTunnel::WindowsSplitTunnel(HANDLE driverIO) : m_driver(driverIO) {
  logger.debug() << "Connected to the Driver";

  Q_ASSERT(getState() == STATE_INITIALIZED);
}

WindowsSplitTunnel::~WindowsSplitTunnel() {
  CloseHandle(m_driver);
  uninstallDriver();
}

bool WindowsSplitTunnel::excludeApps(const QStringList& appPaths) {
  auto state = getState();
  if (state != STATE_READY && state != STATE_RUNNING) {
    logger.warning() << "Driver is not in the right State to set Rules"
                     << state;
    return false;
  }
  return pushAppConfiguration(appPaths);
}

bool WindowsSplitTunnel::pushAppConfiguration(const QStringList& appPaths) {
  logger.debug() << "Pushing new Ruleset for Split-Tunnel " << stateString();
  auto config = generateAppConfiguration(appPaths);
  if (config.empty()) {
    logger.error() << "No valid application paths for the split-tunnel driver:"
                   << appPaths;
    return false;
  }

  DWORD bytesReturned;
  auto ok = DeviceIoControl(m_driver, IOCTL_SET_CONFIGURATION, config.data(),
                            (DWORD)config.size(), nullptr, 0, &bytesReturned,
                            nullptr);
  if (!ok) {
    auto err = GetLastError();
    WindowsUtils::windowsLog("Set Config Failed:");
    logger.error() << "Failed to set Config err code " << err;
    return false;
  }
  logger.debug() << "New Configuration applied: " << stateString();
  return true;
}

bool WindowsSplitTunnel::start(int inetAdapterIndex, int vpnAdapterIndex,
                               const QStringList& excludedApps) {
  // To Start we need to send 2 things:
  // Network info (what is vpn what is network)
  logger.debug() << "Starting SplitTunnel";
  DWORD bytesReturned;

  if (getState() == STATE_STARTED) {
    logger.debug() << "Driver needs Init Call";
    DWORD bytesReturned;
    auto ok = DeviceIoControl(m_driver, IOCTL_INITIALIZE, nullptr, 0, nullptr,
                              0, &bytesReturned, nullptr);
    if (!ok) {
      logger.error() << "Driver init failed";
      return false;
    }
  }

  // Process Info (what is running already)
  if (getState() == STATE_INITIALIZED) {
    logger.debug() << "State is Init, requires process config";
    auto config = generateProcessBlob();
    if (config.empty()) {
      logger.error() << "Failed to build a process snapshot for the driver";
      return false;
    }
    auto ok = DeviceIoControl(m_driver, IOCTL_REGISTER_PROCESSES, config.data(),
                              (DWORD)config.size(), nullptr, 0, &bytesReturned,
                              nullptr);
    if (!ok) {
      logger.error() << "Failed to set Process Config";
      return false;
    }
    logger.debug() << "Set Process Config ok || new State:" << stateString();
  }

  if (getState() == STATE_INITIALIZED) {
    logger.warning() << "Driver is still not ready after process list send";
    return false;
  }
  logger.debug() << "Driver is  ready || new State:" << stateString();

  // Push the exclusion list while the driver is still idle. Registering the IP
  // addresses below moves it to ENGAGED, i.e. it starts enforcing; an app that
  // is only classified after that point keeps its sockets authorized inside the
  // tunnel, and the driver then blocks those flows instead of redirecting them
  // — a running game loses its UDP session and never comes back. Pushing the
  // list first means the driver already knows the app when it engages.
  if (!excludedApps.isEmpty() && !pushAppConfiguration(excludedApps)) {
    logger.error() << "Failed to push the app exclusions, not engaging the driver";
    return false;
  }

  auto config = generateIPConfiguration(inetAdapterIndex, vpnAdapterIndex);
  if (config.empty()) {
    // Leave the driver in READY (nothing is enforced) rather than engaging it
    // with a bogus/zero address: with a half-checked configuration the driver
    // binds the excluded apps to an address that cannot route, which is worse
    // than not excluding them at all.
    logger.error() << "Failed to resolve adapter addresses; internet adapter:"
                   << inetAdapterIndex << "vpn adapter:" << vpnAdapterIndex;
    return false;
  }
  auto ok = DeviceIoControl(m_driver, IOCTL_REGISTER_IP_ADDRESSES, config.data(),
                            (DWORD)config.size(), nullptr, 0, &bytesReturned,
                            nullptr);
  if (!ok) {
    logger.error() << "Failed to set Network Config, error:" << GetLastError()
                   << "state:" << stateString();
    return false;
  }
  logger.debug() << "New Network Config Applied || new State:" << stateString();
  return true;
}

void WindowsSplitTunnel::stop() {
  DWORD bytesReturned;
  auto ok = DeviceIoControl(m_driver, IOCTL_CLEAR_CONFIGURATION, nullptr, 0,
                            nullptr, 0, &bytesReturned, nullptr);
  if (!ok) {
    logger.error() << "Stopping Split tunnel not successfull";
    return;
  }
  logger.debug() << "Stopping Split tunnel successfull";
}

bool WindowsSplitTunnel::resetDriver(HANDLE driverIO) {
  DWORD bytesReturned;
  auto ok = DeviceIoControl(driverIO, IOCTL_ST_RESET, nullptr, 0, nullptr, 0,
                            &bytesReturned, nullptr);
  if (!ok) {
    logger.error() << "Reset Split tunnel not successfull";
    return false;
  }
  logger.debug() << "Reset Split tunnel successfull";
  return true;
}

// static
WindowsSplitTunnel::DRIVER_STATE WindowsSplitTunnel::getState(HANDLE driverIO) {
  if (driverIO == INVALID_HANDLE_VALUE) {
    logger.debug() << "Can't query State from non Opened Driver";
    return STATE_UNKNOWN;
  }
  DWORD bytesReturned;
  SIZE_T outBuffer;
  bool ok = DeviceIoControl(driverIO, IOCTL_GET_STATE, nullptr, 0, &outBuffer,
                            sizeof(outBuffer), &bytesReturned, nullptr);
  if (!ok) {
    WindowsUtils::windowsLog("getState response failure");
    return STATE_UNKNOWN;
  }
  if (bytesReturned == 0) {
    WindowsUtils::windowsLog("getState response is empty");
    return STATE_UNKNOWN;
  }
  return static_cast<WindowsSplitTunnel::DRIVER_STATE>(outBuffer);
}
WindowsSplitTunnel::DRIVER_STATE WindowsSplitTunnel::getState() {
  return getState(m_driver);
}

std::vector<uint8_t> WindowsSplitTunnel::generateAppConfiguration(
    const QStringList& appPaths) {
  // Step 1: Calculate how much size the buffer will need
  size_t cummulated_string_size = 0;
  QStringList dosPaths;
  for (auto const& path : appPaths) {
    auto dosPath = convertPath(path);
    if (dosPath.isEmpty()) {
      // A path that does not convert becomes an empty entry in the driver's
      // image list, i.e. it silently excludes nothing. Drop it instead of
      // pushing garbage: a game left in the tunnel is exactly the "exclusions
      // don't work" report we get from Windows users.
      logger.error() << "Dropping split-tunnel app path, not convertible to a"
                        " device path:"
                     << path;
      continue;
    }
    dosPaths.append(dosPath);
    cummulated_string_size += dosPath.toStdWString().size() * sizeof(wchar_t);
    logger.debug() << dosPath;
  }
  if (dosPaths.isEmpty()) {
    return {};
  }
  size_t bufferSize = sizeof(CONFIGURATION_HEADER) +
                      (sizeof(CONFIGURATION_ENTRY) * dosPaths.size()) +
                      cummulated_string_size;
  std::vector<uint8_t> outBuffer(bufferSize);

  auto header = (CONFIGURATION_HEADER*)&outBuffer[0];
  auto entry = (CONFIGURATION_ENTRY*)(header + 1);

  auto stringDest = &outBuffer[0] + sizeof(CONFIGURATION_HEADER) +
                    (sizeof(CONFIGURATION_ENTRY) * dosPaths.size());

  SIZE_T stringOffset = 0;

  for (const QString& path : dosPaths) {
    auto wstr = path.toStdWString();
    auto cstr = wstr.c_str();
    auto stringLength = wstr.size() * sizeof(wchar_t);

    entry->ImageNameLength = (USHORT)stringLength;
    entry->ImageNameOffset = stringOffset;

    memcpy(stringDest, cstr, stringLength);

    ++entry;
    stringDest += stringLength;
    stringOffset += stringLength;
  }

  header->NumEntries = dosPaths.length();
  header->TotalLength = bufferSize;

  return outBuffer;
}

std::vector<std::byte> WindowsSplitTunnel::generateIPConfiguration(
    int inetAdapterIndex, int vpnAdapterIndex) {
  std::vector<std::byte> out(sizeof(IP_ADDRESSES_CONFIG));

  auto config = reinterpret_cast<IP_ADDRESSES_CONFIG*>(&out[0]);

  if (vpnAdapterIndex == 0) {
    vpnAdapterIndex = WindowsCommons::VPNAdapterIndex();
  }

  AdapterEnumerator adapters;
  bool tunnelResolved =
      vpnAdapterIndex > 0
      && adapters.byIndex(vpnAdapterIndex, &config->TunnelIpv4, &config->TunnelIpv6);
  if (!tunnelResolved) {
    // The tunnel adapter is IPv4-only, and Qt can report its index as 0 when the
    // adapter has no IPv6 index; fall back to its friendly name.
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
      const QString name = iface.humanReadableName();
      if (!name.contains("Dopamine", Qt::CaseInsensitive)) {
        continue;
      }
      if (adapters.byName(name, &config->TunnelIpv4, &config->TunnelIpv6)) {
        tunnelResolved = true;
        break;
      }
    }
  }
  if (!tunnelResolved) {
    logger.error() << "Could not resolve the tunnel adapter (index"
                   << vpnAdapterIndex << "), split tunnel stays passive";
    return {};
  }
  // 2nd best route is usually the internet adapter
  if (!getAddress(inetAdapterIndex, &config->InternetIpv4,
                  &config->InternetIpv6)) {
    logger.error() << "Could not resolve the internet adapter (index"
                   << inetAdapterIndex << "), split tunnel stays passive";
    return {};
  }
  // Never mix these up: Tunnel is the WG adapter, Internet the physical one.
  // Swapping them (or passing the tunnel address as "internet") makes the
  // driver bind every excluded app to an address the app cannot use.
  logger.info() << "Split tunnel addresses - tunnel:" << ipv4ToString(config->TunnelIpv4)
                << ipv6ToString(config->TunnelIpv6)
                << "internet:" << ipv4ToString(config->InternetIpv4)
                << ipv6ToString(config->InternetIpv6);

  if (inetAdapterIndex == vpnAdapterIndex
      || config->TunnelIpv4.s_addr == config->InternetIpv4.s_addr) {
    // Happens when the "route to the server" lookup runs while the tunnel is
    // already the default route (reconnect / server switch): the physical
    // adapter then resolves to the tunnel itself. The driver forbids excluded
    // apps to bind to the tunnel interface and blocks them, i.e. the excluded
    // app loses all connectivity. Refuse the configuration instead.
    logger.error() << "Refusing split-tunnel config: internet adapter resolves"
                      " to the tunnel adapter"
                   << inetAdapterIndex;
    return {};
  }
  return out;
}
bool WindowsSplitTunnel::getAddress(int adapterIndex, IN_ADDR* out_ipv4,
                                    IN6_ADDR* out_ipv6) {
  if (adapterIndex <= 0) {
    logger.warning() << "Invalid adapter index for split-tunnel:" << adapterIndex;
    std::memset(out_ipv4, 0, sizeof(IN_ADDR));
    std::memset(out_ipv6, 0, sizeof(IN6_ADDR));
    return false;
  }
  AdapterEnumerator adapters;
  return adapters.byIndex(adapterIndex, out_ipv4, out_ipv6);
}

std::vector<uint8_t> WindowsSplitTunnel::generateProcessBlob() {
  // Get a Snapshot of all processes that are running:
  HANDLE snapshot_handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot_handle == INVALID_HANDLE_VALUE) {
    WindowsUtils::windowsLog("Creating Process snapshot failed");
    return std::vector<uint8_t>(0);
  }
  auto cleanup = qScopeGuard([&] { CloseHandle(snapshot_handle); });
  // Load the First Entry, later iterate over all
  PROCESSENTRY32W currentProcess;
  currentProcess.dwSize = sizeof(PROCESSENTRY32W);

  if (FALSE == (Process32First(snapshot_handle, &currentProcess))) {
    WindowsUtils::windowsLog("Cant read first entry");
  }

  QMap<DWORD, ProcessInfo> processes;

  do {
    auto process_handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                      currentProcess.th32ProcessID);

    if (process_handle == INVALID_HANDLE_VALUE) {
      continue;
    }
    ProcessInfo info = getProcessInfo(process_handle, currentProcess);
    processes.insert(info.ProcessId, info);
    CloseHandle(process_handle);

  } while (FALSE != (Process32NextW(snapshot_handle, &currentProcess)));

  auto process_list = processes.values();
  if (process_list.isEmpty()) {
    logger.debug() << "Process Snapshot list was empty";
    return std::vector<uint8_t>(0);
  }

  logger.debug() << "Reading Processes NUM: " << process_list.size();
  // Determine the Size of the outBuffer:
  size_t totalStringSize = 0;

  for (const auto& process : process_list) {
    totalStringSize += (process.DevicePath.size() * sizeof(wchar_t));
  }
  auto bufferSize = sizeof(PROCESS_DISCOVERY_HEADER) +
                    (sizeof(PROCESS_DISCOVERY_ENTRY) * processes.size()) +
                    totalStringSize;

  std::vector<uint8_t> out(bufferSize);

  auto header = reinterpret_cast<PROCESS_DISCOVERY_HEADER*>(&out[0]);
  auto entry = reinterpret_cast<PROCESS_DISCOVERY_ENTRY*>(header + 1);
  auto stringBuffer = reinterpret_cast<uint8_t*>(entry + processes.size());

  SIZE_T currentStringOffset = 0;

  for (const auto& process : process_list) {
    // Wierd DWORD -> Handle Pointer magic.
    entry->ProcessId = (HANDLE)((size_t)process.ProcessId);
    entry->ParentProcessId = (HANDLE)((size_t)process.ParentProcessId);

    if (process.DevicePath.empty()) {
      entry->ImageNameOffset = 0;
      entry->ImageNameLength = 0;
    } else {
      const auto imageNameLength = process.DevicePath.size() * sizeof(wchar_t);

      entry->ImageNameOffset = currentStringOffset;
      entry->ImageNameLength = static_cast<USHORT>(imageNameLength);

      RtlCopyMemory(stringBuffer + currentStringOffset, &process.DevicePath[0],
                    imageNameLength);

      currentStringOffset += imageNameLength;
    }
    ++entry;
  }

  header->NumEntries = processes.size();
  header->TotalLength = bufferSize;

  return out;
}

// static
SC_HANDLE WindowsSplitTunnel::installDriver() {
  LPCWSTR displayName = L"Dopamine Split Tunnel Service";
  QFileInfo driver(qApp->applicationDirPath() + "/" + DRIVER_FILENAME);
  if (!driver.exists()) {
    logger.error() << "Split Tunnel Driver File not found "
                   << driver.absoluteFilePath();
    return (SC_HANDLE)INVALID_HANDLE_VALUE;
  }
  auto path = driver.absolutePath() + "/" + DRIVER_FILENAME;
  auto binPath = (const wchar_t*)path.utf16();
  auto scm_rights = SC_MANAGER_ALL_ACCESS;
  auto serviceManager = OpenSCManager(nullptr,  // local computer
                                      nullptr,  // servicesActive database
                                      scm_rights);
  auto service = CreateService(
      serviceManager, DRIVER_SERVICE_NAME, displayName, SERVICE_ALL_ACCESS,
      SERVICE_KERNEL_DRIVER, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, binPath,
      nullptr, nullptr, nullptr, nullptr, nullptr);
  CloseServiceHandle(serviceManager);
  return service;
}
// static
bool WindowsSplitTunnel::uninstallDriver() {
  removeLegacyDriverService();
  auto scm_rights = SC_MANAGER_ALL_ACCESS;
  auto serviceManager = OpenSCManager(NULL,  // local computer
                                      NULL,  // servicesActive database
                                      scm_rights);

  auto servicehandle =
      OpenService(serviceManager, DRIVER_SERVICE_NAME, GENERIC_READ);
  auto result = DeleteService(servicehandle);
  if (result) {
    logger.debug() << "Split Tunnel Driver Removed";
  }
  return result;
}
// static
bool WindowsSplitTunnel::isInstalled() {
  // Check if the Drivers I/O File is present
  auto symlink = QFileInfo(QString::fromWCharArray(DRIVER_SYMLINK));
  if (symlink.exists()) {
    return true;
  }
  // If not check with SCM, if the kernel service exists
  auto scm_rights = SC_MANAGER_ALL_ACCESS;
  auto serviceManager = OpenSCManager(NULL,  // local computer
                                      NULL,  // servicesActive database
                                      scm_rights);
  auto servicehandle =
      OpenService(serviceManager, DRIVER_SERVICE_NAME, GENERIC_READ);
  auto err = GetLastError();
  CloseServiceHandle(serviceManager);
  CloseServiceHandle(servicehandle);
  return err != ERROR_SERVICE_DOES_NOT_EXIST;
}

// static
void WindowsSplitTunnel::removeLegacyDriverService() {
  auto serviceManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
  if (serviceManager == nullptr) {
    return;
  }
  auto service = OpenService(serviceManager, LEGACY_DRIVER_SERVICE_NAME, DELETE);
  if (service != nullptr) {
    // marked for deletion; the loaded driver keeps running until reboot
    if (DeleteService(service)) {
      logger.info() << "Legacy AmneziaVPNSplitTunnel service removed";
    }
    CloseServiceHandle(service);
  }
  CloseServiceHandle(serviceManager);
}

QString WindowsSplitTunnel::convertPath(const QString& path) {
  // Accept what the UI and the settings file can hand us: a file:// URL from
  // the QML file dialog, a native "C:\Program Files\..." path or the Qt-flavoured
  // "C:/Program Files/..." one. Splitting on "/" only (the old behaviour) turned
  // every backslash path into an empty device path, i.e. the app was silently
  // never excluded.
  QString normalized = path.trimmed();
  if (normalized.startsWith("file:", Qt::CaseInsensitive)) {
    const QString localPath = QUrl(normalized).toLocalFile();
    if (!localPath.isEmpty()) {
      normalized = localPath;
    }
  }
  normalized.replace('/', '\\');

  auto parts = normalized.split('\\', Qt::SkipEmptyParts);
  if (parts.size() < 2) {
    return "";
  }
  QString driveLetter = parts.takeFirst();
  if (driveLetter.size() != 2 || driveLetter.at(1) != ':') {
    // Only drive-letter paths have a stable \Device\HarddiskVolumeN mapping;
    // UNC paths would need a different (volume GUID) conversion.
    return "";
  }
  QByteArray buffer(2048, 0xFFu);
  auto ok = QueryDosDeviceW(qUtf16Printable(driveLetter),
                            (wchar_t*)buffer.data(), buffer.size() / 2);

  if (ok == ERROR_INSUFFICIENT_BUFFER) {
    buffer.resize(buffer.size() * 2);
    ok = QueryDosDeviceW(qUtf16Printable(driveLetter), (wchar_t*)buffer.data(),
                         buffer.size() / 2);
  }
  if (ok == 0) {
    WindowsUtils::windowsLog("Err fetching dos path");
    return "";
  }
  QString deviceName;
  deviceName = QString::fromWCharArray((wchar_t*)buffer.data());
  parts.prepend(deviceName);

  return parts.join("\\");
}

// static
bool WindowsSplitTunnel::detectConflict() {
  auto scm_rights = SC_MANAGER_ENUMERATE_SERVICE;
  auto serviceManager = OpenSCManager(NULL,  // local computer
                                      NULL,  // servicesActive database
                                      scm_rights);
  auto cleanup = qScopeGuard([&] { CloseServiceHandle(serviceManager); });
  // Query for Mullvad Service.
  auto servicehandle =
      OpenService(serviceManager, MV_SERVICE_NAME, GENERIC_READ);
  auto err = GetLastError();
  CloseServiceHandle(servicehandle);
  if (err != ERROR_SERVICE_DOES_NOT_EXIST) {
    WindowsUtils::windowsLog("Mullvad Detected - Disabling SplitTunnel: ");
    // Mullvad is installed, so we would certainly break things.
    return true;
  }
  auto symlink = QFileInfo(QString::fromWCharArray(DRIVER_SYMLINK));
  if (!symlink.exists()) {
    // The driver is not loaded / installed.. MV is not installed, all good!
    logger.info() << "No Split-Tunnel Conflict detected, continue.";
    return false;
  }
  // The driver exists, so let's check if it has been created by us (current
  // or pre-rebrand registration). If our service is not present, it has been
  // created by someone else so we should not use that :)
  servicehandle = OpenService(serviceManager, DRIVER_SERVICE_NAME, GENERIC_READ);
  err = GetLastError();
  CloseServiceHandle(servicehandle);
  if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
    servicehandle = OpenService(serviceManager, LEGACY_DRIVER_SERVICE_NAME, GENERIC_READ);
    err = GetLastError();
    CloseServiceHandle(servicehandle);
  }
  return err == ERROR_SERVICE_DOES_NOT_EXIST;
}

bool WindowsSplitTunnel::isRunning() { return getState() == STATE_RUNNING; }
QString WindowsSplitTunnel::stateString() {
  switch (getState()) {
    case STATE_UNKNOWN:
      return "STATE_UNKNOWN";
    case STATE_NONE:
      return "STATE_NONE";
    case STATE_STARTED:
      return "STATE_STARTED";
    case STATE_INITIALIZED:
      return "STATE_INITIALIZED";
    case STATE_READY:
      return "STATE_READY";
    case STATE_RUNNING:
      return "STATE_RUNNING";
    case STATE_ZOMBIE:
      return "STATE_ZOMBIE";
      break;
  }
  return {};
}
