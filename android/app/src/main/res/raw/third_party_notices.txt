# Third-party code and build inputs

The bundled `shared/security2.mjs` retains Espressif Systems' 2018–2026 copyright and Apache-2.0 header. It derives from ESP-IDF v5.5.5 `tools/esp_prov/security/security2.py` and `srp6a.py`. The connection module and public synthetic reference tests derive from this project's existing phone/desktop modules. Both runtime JS modules are copied unchanged from commit `a3003665f0a0a92f8bc66b470f662cebb97756eb`; hashes are in `shared-provenance.json`. Retained prototype comments are historical provenance; the Android app supplies a personal login, never a public fixture. JavaScript BigInt SRP is not constant-time, managed strings cannot guarantee erasure, and this reuse is not an independent cryptographic security review.

Runtime dependency pins (all Apache-2.0):

- AndroidX WebKit 1.14.0; Annotation/Annotation JVM 1.8.1; Annotation Experimental 1.4.1.
- AndroidX Core 1.1.0; Lifecycle Runtime/Common 2.0.0; Arch Core Common 2.0.0; VersionedParcelable 1.1.0; Collection 1.0.0.
- Kotlin Standard Library/Common 1.7.10; JetBrains Annotations 13.0; JSpecify 1.0.0.

Android System WebView is supplied and updated by the phone OS, not bundled in this APK. The app requires the supported WebMessageListener feature at runtime. No analytics, hosted scripts, crypto package or browser Bluetooth dependency is added.

Build/test inputs are separate: Android Gradle Plugin 8.13.2 and Gradle Wrapper 8.13 (Apache-2.0); JUnit 4.13.2 (EPL-1.0) with Hamcrest Core 1.3 (BSD-3-Clause); Temurin JDK 17.0.21+9 (GPL-2.0 with Classpath Exception); Android SDK/build-tools under their supplied Android SDK terms. These tools/test JARs are not packaged as app runtime. Gradle's resolved dependency checksums are retained in `gradle/verification-metadata.xml`; initial checksums were recorded from the official Google/Maven Central resolution and require the usual dependency trust review, not a claim of independent source auditing.

The complete Apache-2.0 license is supplied in `licenses/Apache-2.0.txt` and as an APK raw resource. Runtime notices are also an APK raw resource. Upstream sources/licenses: [AndroidX](https://android.googlesource.com/platform/frameworks/support/), [Kotlin](https://github.com/JetBrains/kotlin), [JSpecify](https://github.com/jspecify/jspecify), [Espressif Security2](https://github.com/espressif/esp-idf/tree/v5.5.5/tools/esp_prov/security), [JUnit](https://github.com/junit-team/junit4), [Hamcrest](https://github.com/hamcrest/JavaHamcrest), [Gradle](https://github.com/gradle/gradle), [Temurin](https://adoptium.net/temurin/releases/).

## New dashboard-only source

The new independent `dashboard.mjs`, `DashboardClient.java` and their named
new tests carry SPDX MIT notices. The grant below applies only to those files;
it does not relicense inherited Android, protocol, firmware, artwork or other code.

Copyright (c) 2026 Cloud Stick Pro contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of these files and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
