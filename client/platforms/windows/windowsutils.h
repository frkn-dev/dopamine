/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WINDOWSUTILS_H
#define WINDOWSUTILS_H

#include <QString>

class WindowsUtils final {
 public:
  static QString getErrorMessage();
  static QString getErrorMessage(quint32 code);
  static void windowsLog(const QString& msg);

  // Returns the major version of Windows
  static QString windowsVersion();

  // Force an application crash for testing
  static void forceCrash();

  // Make sure the dopamine-service process is running: tries an elevated
  // `net start`, and if the service was never installed (silent MSI
  // failure), installs and starts it elevated. Returns true when the
  // service process is up. Shows a UAC prompt; returns false if the user
  // declines or the recovery fails.
  static bool ensureDopamineServiceRunning();
};

#endif  // WINDOWSUTILS_H
