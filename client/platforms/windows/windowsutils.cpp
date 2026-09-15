/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "windowsutils.h"

#include <Windows.h>
#include <errhandlingapi.h>
#include <shellapi.h>

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
  sei.lpVerb = L"runas";
  sei.lpFile = file.c_str();
  sei.lpParameters = parameters.c_str();
  sei.nShow = SW_HIDE;

  if (!ShellExecuteExW(&sei)) {
    // ERROR_CANCELLED = the user declined the UAC prompt
    logger.warning() << "runElevatedAndWait: ShellExecuteEx failed for"
                     << program << "-" << WindowsUtils::getErrorMessage();
    return false;
  }

  WaitForSingleObject(sei.hProcess, 30000);
  DWORD exitCode = 1;
  GetExitCodeProcess(sei.hProcess, &exitCode);
  CloseHandle(sei.hProcess);
  return exitCode == 0;
}

}  // namespace

// static
bool WindowsUtils::ensureDopamineServiceRunning() {
  const QString serviceProcess = Utils::executable(SERVICE_NAME, false);
  if (Utils::processIsRunning(serviceProcess, true)) {
    return true;
  }

  logger.debug() << "ensureDopamineServiceRunning: service is not running,"
                    " trying an elevated start";

  // service installed but stopped (crashed without recovery, stopped by an
  // upgrade, killed by AV) — a plain elevated start fixes it
  runElevatedAndWait(QStringLiteral("net.exe"),
                     QStringLiteral("start %1").arg(QString::fromLatin1(SERVICE_NAME)));

  // not installed at all (MSI ran without enough privileges and Vital="no"
  // used to swallow the failure) — install elevated, then start
  if (!Utils::processIsRunning(serviceProcess, true)) {
    const QString serviceExe = Utils::executable(SERVICE_NAME, true);
    if (QFileInfo::exists(serviceExe)) {
      logger.debug() << "ensureDopamineServiceRunning: net start failed,"
                        " installing the service elevated";
      runElevatedAndWait(serviceExe, QStringLiteral("-i"));
      runElevatedAndWait(QStringLiteral("net.exe"),
                         QStringLiteral("start %1").arg(QString::fromLatin1(SERVICE_NAME)));
    }
  }

  // give the SCM a moment to spawn the process, then do the final check
  for (int i = 0; i < 20; ++i) {
    if (Utils::processIsRunning(serviceProcess, true)) {
      logger.debug() << "ensureDopamineServiceRunning: service is up";
      return true;
    }
    Sleep(250);
  }

  logger.warning() << "ensureDopamineServiceRunning: service is still down";
  return false;
}
