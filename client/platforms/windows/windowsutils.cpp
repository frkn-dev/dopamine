/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "windowsutils.h"

#include <Windows.h>
#include <errhandlingapi.h>
#include <shellapi.h>
#include <winsvc.h>

#include <QFileInfo>
#include <QSettings>
#include <QSysInfo>

#include "logger.h"
#include "utilities.h"
#include "version.h"

namespace {
Logger logger("WindowsUtils");
}  // namespace

constexpr const int WINDOWS_11_BUILD =
    22000;  // Build Number of the first release win 11 iso

QString WindowsUtils::getErrorMessage(quint32 code) {
  LPSTR messageBuffer = nullptr;
  size_t size = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      (LPSTR)&messageBuffer, 0, nullptr);

  std::string message(messageBuffer, size);
  QString result(message.c_str());
  LocalFree(messageBuffer);
  return result;
}

QString WindowsUtils::getErrorMessage() {
  return getErrorMessage(GetLastError());
}

// A simple function to log windows error messages.
void WindowsUtils::windowsLog(const QString& msg) {
  QString errmsg = getErrorMessage();
  logger.error() << msg << "-" << errmsg;
}

// Static
QString WindowsUtils::windowsVersion() {
  QSettings regCurrentVersion(
      "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
      QSettings::NativeFormat);

  int buildNr = regCurrentVersion.value("CurrentBuild").toInt();
  if (buildNr >= WINDOWS_11_BUILD) {
    return "11";
  }
  return QSysInfo::productVersion();
}

// static
void WindowsUtils::forceCrash() {
  RaiseException(0x0000DEAD, EXCEPTION_NONCONTINUABLE, 0, NULL);
}

namespace {

// Launch a program elevated ("runas" → UAC prompt) and wait for it to exit.
// Returns true only when the process ran and exited with code 0.
bool runElevatedAndWait(const QString& program, const QString& params) {
  const std::wstring file = program.toStdWString();
  const std::wstring parameters = params.toStdWString();

  SHELLEXECUTEINFOW sei = {};
  sei.cbSize = sizeof(sei);
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;
  sei.hwnd = GetForegroundWindow();
  sei.lpVerb = L"runas";
  sei.lpFile = file.c_str();
  sei.lpParameters = parameters.c_str();
  sei.nShow = SW_HIDE;

  if (!ShellExecuteExW(&sei) || !sei.hProcess) {
    // ERROR_CANCELLED = the user declined the UAC prompt
    logger.warning() << "runElevatedAndWait: ShellExecuteEx failed for"
                     << program << "-" << WindowsUtils::getErrorMessage();
    return false;
  }

  // long enough for the UAC prompt plus sc.exe, which waits out a slow start
  const DWORD wait = WaitForSingleObject(sei.hProcess, 90000);
  DWORD exitCode = 1;
  if (wait == WAIT_OBJECT_0) {
    GetExitCodeProcess(sei.hProcess, &exitCode);
  }
  CloseHandle(sei.hProcess);
  if (exitCode != 0) {
    logger.debug() << "runElevatedAndWait:" << program << "exit" << exitCode;
  }
  return exitCode == 0;
}

QString scExePath() {
  wchar_t systemDir[MAX_PATH] = {};
  GetSystemDirectoryW(systemDir, MAX_PATH);
  return QString::fromWCharArray(systemDir) + QStringLiteral("\\sc.exe");
}

struct ServiceQuery {
  bool exists = false;
  DWORD state = 0;
  DWORD pid = 0;
};

// SERVICE_QUERY_STATUS is granted to interactive users; START is not.
ServiceQuery queryService(const wchar_t* name) {
  ServiceQuery query;
  const SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) {
    return query;
  }
  const SC_HANDLE service = OpenServiceW(scm, name, SERVICE_QUERY_STATUS);
  if (!service) {
    CloseServiceHandle(scm);
    return query;
  }
  query.exists = true;
  SERVICE_STATUS_PROCESS status = {};
  DWORD needed = 0;
  if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status),
                           sizeof(status), &needed)) {
    query.state = status.dwCurrentState;
    query.pid = status.dwProcessId;
  }
  CloseServiceHandle(service);
  CloseServiceHandle(scm);
  return query;
}

// OpenProcess on a SYSTEM service often fails with ACCESS_DENIED while the
// process is alive. ERROR_INVALID_PARAMETER means the pid is gone.
bool pidAlive(DWORD pid) {
  if (pid == 0) {
    return false;
  }
  const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) {
    return GetLastError() == ERROR_ACCESS_DENIED;
  }
  DWORD code = 0;
  const bool alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
  CloseHandle(process);
  return alive;
}

}  // namespace

// static
bool WindowsUtils::ensureDopamineServiceRunning() {
  const QString serviceProcess = Utils::executable(SERVICE_NAME, false);
  const auto running = [&]() { return Utils::processIsRunning(serviceProcess, true); };
  if (running()) {
    return true;
  }

  const QString serviceName = QString::fromLatin1(SERVICE_NAME);
  const std::wstring serviceNameW = serviceName.toStdWString();
  ServiceQuery query = queryService(serviceNameW.c_str());

  // A killed service process stays STOP_PENDING (or RUNNING with a dead pid)
  // until the SCM notices. `sc start` in that window fails, and reinstalling
  // the service makes it worse — wait, then start the existing service.
  if (query.exists && (query.state == SERVICE_STOP_PENDING || query.state == SERVICE_START_PENDING
                       || (query.state == SERVICE_RUNNING && !pidAlive(query.pid)))) {
    logger.debug() << "ensureDopamineServiceRunning: service is wedged, waiting for SCM";
    if (query.state == SERVICE_RUNNING) {
      runElevatedAndWait(scExePath(), QStringLiteral("stop %1").arg(serviceName));
    }
    for (int i = 0; i < 40 && !running(); ++i) {
      query = queryService(serviceNameW.c_str());
      if (query.state == SERVICE_STOPPED) {
        break;
      }
      Sleep(250);
    }
  }

  if (!running() && !query.exists) {
    // not installed at all (MSI ran without enough privileges) — install, then start
    const QString serviceExe = Utils::executable(SERVICE_NAME, true);
    if (!QFileInfo::exists(serviceExe)) {
      logger.warning() << "ensureDopamineServiceRunning: service executable missing";
      return false;
    }
    logger.debug() << "ensureDopamineServiceRunning: service is not installed, installing";
    if (!runElevatedAndWait(serviceExe, QStringLiteral("-i"))) {
      return false;
    }
  }

  if (!running()) {
    logger.debug() << "ensureDopamineServiceRunning: starting the service";
    runElevatedAndWait(scExePath(), QStringLiteral("start %1").arg(serviceName));
  }

  for (int i = 0; i < 60; ++i) {
    if (running()) {
      logger.debug() << "ensureDopamineServiceRunning: service is up";
      // the process is up before its IPC socket listens
      Sleep(500);
      return true;
    }
    Sleep(250);
  }

  logger.warning() << "ensureDopamineServiceRunning: service is still down";
  return false;
}
