#include <QtGlobal>

#if defined(Q_OS_WIN)

// qt_windows.h (not a bare windows.h after Qt) so wincrypt.h sees ULONG_PTR.
#include <qt_windows.h>
#include <SoftPub.h>
#include <WinTrust.h>
#include <wincrypt.h>

#include <QDebug>
#include <QDir>
#include <QString>

#include <string>
#include <vector>

#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")

bool windowsPackageIsTrusted(const QString &path)
{
    // MSI is signed with the "Privacy Technologies OU" certificate (see deploy/build_windows.bat).
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();

    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = native.c_str();

    WINTRUST_DATA trustData{};
    trustData.cbStruct = sizeof(trustData);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.pFile = &fileInfo;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;
    trustData.dwProvFlags = WTD_SAFER_FLAG;

    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG status = WinVerifyTrust(nullptr, &policy, &trustData);
    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &policy, &trustData);
    if (status != ERROR_SUCCESS) {
        qWarning() << "[UPDATE] authenticode status" << status;
        return false;
    }

    HCERTSTORE store = nullptr;
    HCRYPTMSG msg = nullptr;
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, native.c_str(), CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                          CERT_QUERY_FORMAT_FLAG_BINARY, 0, nullptr, nullptr, nullptr, &store, &msg, nullptr)) {
        qWarning() << "[UPDATE] CryptQueryObject failed" << GetLastError();
        return false;
    }

    bool trusted = false;
    DWORD signerCount = 0;
    DWORD signerCountSize = sizeof(signerCount);
    if (CryptMsgGetParam(msg, CMSG_SIGNER_COUNT_PARAM, 0, &signerCount, &signerCountSize) && signerCount > 0) {
        DWORD signerInfoSize = 0;
        CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &signerInfoSize);
        if (signerInfoSize > 0 && signerInfoSize <= 65536) {
            std::vector<unsigned char> signerInfoBuf(signerInfoSize);
            if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, signerInfoBuf.data(), &signerInfoSize)) {
                auto *signerInfo = reinterpret_cast<CMSG_SIGNER_INFO *>(signerInfoBuf.data());
                CERT_INFO certInfo{};
                certInfo.Issuer = signerInfo->Issuer;
                certInfo.SerialNumber = signerInfo->SerialNumber;
                PCCERT_CONTEXT cert = CertFindCertificateInStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
                                                                 CERT_FIND_SUBJECT_CERT, &certInfo, nullptr);
                if (cert) {
                    wchar_t name[512];
                    const DWORD nameLen = CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 512);
                    const QString signer = nameLen > 1 ? QString::fromWCharArray(name) : QString();
                    qInfo() << "[UPDATE] signer" << signer;
                    trusted = signer.contains(QStringLiteral("Privacy Technologies OU"), Qt::CaseInsensitive);
                    CertFreeCertificateContext(cert);
                }
            }
        }
    }

    CertCloseStore(store, 0);
    CryptMsgClose(msg);
    return trusted;
}

#endif
