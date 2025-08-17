Generate key for signing the app
```bash
keytool -genkey -v -keystore frkn-release-key.jks -keyalg RSA -keysize 2048 -validity 10000 -alias frkn-key
```

Add the following to your `~/.bashrc` or `~/.bash_profile` file
```
export ANDROID_KEYSTORE_PATH="/path/to/file/frkn-release-key.jks"
export ANDROID_KEYSTORE_KEY_PASS="frkn_jks_password"
export ANDROID_KEYSTORE_KEY_ALIAS="frkn-key"
```

Build apks
```bash
./deploy/build_android.sh --aab --apk all -m
```

Sign after CI build, change PASS to actual password
```bash
jarsigner \
  -keystore frkn-release-key.jks \
  -storepass "PASS" \
  -keypass "PASS" \
  -sigalg SHA256withRSA \
  -digestalg SHA-256 \
  FRKN-release.aab \
  frkn-key
```