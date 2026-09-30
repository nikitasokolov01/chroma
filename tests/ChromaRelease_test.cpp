// SPDX-License-Identifier: GPL-3.0-only
#include <QJsonObject>
#include <QTest>

#include "updater/ChromaRelease.h"

namespace {
const QString portableName = "Chroma-0.3.0-Windows-x64.zip";
const QString installerName = "Chroma-0.3.0-Windows-x64-Setup.exe";
const QByteArray digest = QByteArray(64, 'a');

QJsonObject asset(const QString& tag, const QString& name, qint64 size = 1024)
{
    return { { "name", name },
             { "browser_download_url", "https://github.com/nikitasokolov01/chroma/releases/download/" + tag + "/" + name },
             { "size", size } };
}

QJsonObject release(const QString& tag, bool prerelease = false)
{
    QString version = tag;
    if (version.startsWith('v'))
        version.remove(0, 1);
    return { { "tag_name", tag },
             { "draft", false },
             { "prerelease", prerelease },
             { "body", "Release notes" },
             { "assets",
               QJsonArray{ asset(tag, "Chroma-" + version + "-Windows-x64.zip"), asset(tag, "Chroma-" + version + "-Windows-x64-Setup.exe"),
                           asset(tag, "SHA256SUMS.txt"), asset(tag, "package-manifest.json") } } };
}

QJsonObject replaceAsset(QJsonObject candidate, int index, const QJsonObject& replacement)
{
    auto assets = candidate.value("assets").toArray();
    assets[index] = replacement;
    candidate["assets"] = assets;
    return candidate;
}

std::optional<ChromaUpdate::Release> select(const QJsonArray& releases,
                                            bool portable = true,
                                            bool prereleases = false,
                                            const QString& current = "0.2.0")
{
    return ChromaUpdate::selectRelease(releases, *ChromaUpdate::Version::parse(current), portable, prereleases);
}
}  // namespace

class ChromaReleaseTest : public QObject {
    Q_OBJECT
   private slots:
    void validVersions_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("normalized");
        QTest::addColumn<QStringList>("prerelease");
        QTest::newRow("zero") << "0.0.0"
                              << "0.0.0" << QStringList{};
        QTest::newRow("tag-prefix") << "v0.3.0"
                                    << "0.3.0" << QStringList{};
        QTest::newRow("prerelease") << "1.2.3-rc.12"
                                    << "1.2.3-rc.12" << QStringList{ "rc", "12" };
        QTest::newRow("build-metadata") << "v1.2.3+build.007"
                                        << "1.2.3+build.007" << QStringList{};
        QTest::newRow("prerelease-and-build") << "1.2.3-0.alpha-1+sha.42"
                                              << "1.2.3-0.alpha-1+sha.42" << QStringList{ "0", "alpha-1" };
        QTest::newRow("unbounded-integer") << "999999999999999999999999999999.0.0"
                                           << "999999999999999999999999999999.0.0" << QStringList{};
    }

    void validVersions()
    {
        QFETCH(QString, text);
        QFETCH(QString, normalized);
        QFETCH(QStringList, prerelease);
        const auto version = ChromaUpdate::Version::parse(text);
        QVERIFY(version);
        QCOMPARE(version->text, normalized);
        QCOMPARE(version->core.size(), 3);
        QCOMPARE(version->prerelease, prerelease);
    }

    void invalidVersions_data()
    {
        QTest::addColumn<QString>("text");
        const QStringList cases{
            "",         "1",           "1.2",    "1.2.3.4", "V1.2.3",         "vv1.2.3",        "01.2.3",     "1.02.3",        "1.2.03",
            "1.2.3-01", "1.2.3-rc.01", "1.2.3-", "1.2.3+",  "1.2.3-alpha..1", "1.2.3+build..2", "1.2.3-rc_1", "1.2.3+build_1", " 1.2.3",
            "1.2.3 ",   "1.2.3\n",     "-1.2.3"
        };
        for (int i = 0; i < cases.size(); ++i)
            QTest::newRow(qPrintable(QString("invalid-%1").arg(i))) << cases[i];
        QTest::newRow("too-long") << QString("1.2.3+") + QString(123, 'a');
    }

    void invalidVersions()
    {
        QFETCH(QString, text);
        QVERIFY(!ChromaUpdate::Version::parse(text));
    }

    void semanticOrderingDoesNotDependOnIntegerWidth()
    {
        const QStringList ordered{ "0.9.99",
                                   "1.0.0-alpha",
                                   "1.0.0-alpha.1",
                                   "1.0.0-alpha.beta",
                                   "1.0.0-beta",
                                   "1.0.0-beta.2",
                                   "1.0.0-beta.11",
                                   "1.0.0-rc.1",
                                   "1.0.0",
                                   "1.0.1",
                                   "1.1.0",
                                   "2.0.0",
                                   "10.0.0",
                                   "999999999999999999999999999999.0.0",
                                   "1000000000000000000000000000000.0.0" };
        for (int i = 0; i < ordered.size(); ++i) {
            const auto left = ChromaUpdate::Version::parse(ordered[i]);
            QVERIFY(left);
            QCOMPARE(left->compare(*left), 0);
            for (int j = i + 1; j < ordered.size(); ++j) {
                const auto right = ChromaUpdate::Version::parse(ordered[j]);
                QVERIFY(right);
                QVERIFY2(left->compare(*right) < 0, qPrintable(ordered[i] + " < " + ordered[j]));
                QVERIFY(right->compare(*left) > 0);
            }
        }
        const auto numeric = ChromaUpdate::Version::parse("1.0.0-999999999999999999999999999999");
        const auto larger = ChromaUpdate::Version::parse("1.0.0-1000000000000000000000000000000");
        const auto alphabetic = ChromaUpdate::Version::parse("1.0.0-A");
        QVERIFY(numeric && larger && alphabetic);
        QVERIFY(numeric->compare(*larger) < 0);
        QVERIFY(larger->compare(*alphabetic) < 0);
        QCOMPARE(ChromaUpdate::Version::parse("v1.2.3+one")->compare(*ChromaUpdate::Version::parse("1.2.3+two")), 0);
        QCOMPARE(ChromaUpdate::Version::parse("1.2.3-rc.1+one")->compare(*ChromaUpdate::Version::parse("1.2.3-rc.1+two")), 0);
    }

    void selectsHighestUsableReleaseRegardlessOfListOrder()
    {
        auto unusable = release("v3.0.0");
        unusable["assets"] = QJsonArray{};
        for (const auto& candidates : { QJsonArray{ release("v0.3.0"), unusable, release("v1.0.0"), release("v0.4.0") },
                                        QJsonArray{ release("v0.4.0"), release("v1.0.0"), unusable, release("v0.3.0") } }) {
            const auto selected = select(candidates);
            QVERIFY(selected);
            QCOMPARE(selected->tag, QString("v1.0.0"));
            QCOMPARE(selected->notes, QString("Release notes"));
            QVERIFY(!selected->prerelease);
        }
    }

    void prereleasePolicyChecksBothTagAndGitHubFlag()
    {
        const auto stable = release("v0.3.0");
        const auto tagged = release("v0.4.0-rc.1", false);
        const auto flagged = release("v0.5.0", true);
        const auto selectedStable = select({ tagged, flagged, stable });
        QVERIFY(selectedStable);
        QCOMPARE(selectedStable->tag, QString("v0.3.0"));
        QVERIFY(!select({ tagged, flagged }));
        const auto selectedFlagged = select({ stable, flagged, tagged }, true, true);
        QVERIFY(selectedFlagged);
        QCOMPARE(selectedFlagged->tag, QString("v0.5.0"));
        QVERIFY(selectedFlagged->prerelease);
        const auto selectedTagged = select({ tagged }, true, true);
        QVERIFY(selectedTagged);
        QVERIFY(selectedTagged->prerelease);
        const auto final = select({ release("v0.4.0") }, true, false, "0.4.0-rc.1");
        QVERIFY(final);
        QCOMPARE(final->tag, QString("v0.4.0"));
    }

    void neverDowngradesOrReinstallsEquivalentVersions()
    {
        QVERIFY(!select({ release("v0.1.0"), release("v0.2.0"), release("v0.2.0+rebuilt"), release("v0.2.0-rc.9") }, true, true));
        QVERIFY(!select({ release("v0.3.0-rc.1") }, true, true, "0.3.0"));
        QVERIFY(!select({ release("v0.3.0-rc.1+rebuilt") }, true, true, "0.3.0-rc.1+original"));
    }

    void choosesExactPackageForInstallationType()
    {
        const auto candidate = release("v0.3.0");
        const auto portable = select({ candidate });
        const auto installed = select({ candidate }, false);
        QVERIFY(portable && installed);
        QCOMPARE(portable->package.name, portableName);
        QCOMPARE(installed->package.name, installerName);
        QCOMPARE(portable->checksums.name, QString("SHA256SUMS.txt"));
        QCOMPARE(portable->manifest.name, QString("package-manifest.json"));
        auto onlyPortable = candidate;
        auto assets = candidate.value("assets").toArray();
        assets.removeAt(1);
        onlyPortable["assets"] = assets;
        QVERIFY(select({ onlyPortable }));
        QVERIFY(!select({ onlyPortable }, false));
    }

    void rejectsMissingDuplicateOrMisnamedAssets()
    {
        const auto candidate = release("v0.3.0");
        for (int index : { 0, 2, 3 }) {
            auto missing = candidate;
            auto assets = candidate.value("assets").toArray();
            assets.removeAt(index);
            missing["assets"] = assets;
            QVERIFY(!select({ missing }));
            auto duplicate = candidate;
            assets = candidate.value("assets").toArray();
            assets.append(assets[index]);
            duplicate["assets"] = assets;
            QVERIFY(!select({ duplicate }));
            auto renamed = candidate.value("assets").toArray()[index].toObject();
            renamed["name"] = renamed.value("name").toString().toUpper();
            QVERIFY(!select({ replaceAsset(candidate, index, renamed) }));
        }
        QVERIFY(!select({ replaceAsset(candidate, 0, asset("v0.3.0", "Chroma-0.3.0-Windows-arm64.zip")) }));
        QVERIFY(!select({ replaceAsset(candidate, 0, asset("v0.3.0", "Chroma-0.2.0-Windows-x64.zip")) }));
    }

    void rejectsUntrustedInitialAssetAddresses_data()
    {
        QTest::addColumn<QString>("url");
        const QString path = "/nikitasokolov01/chroma/releases/download/v0.3.0/" + portableName;
        QTest::newRow("http") << "http://github.com" + path;
        QTest::newRow("host-suffix") << "https://github.com.evil.example" + path;
        QTest::newRow("host-prefix") << "https://evilgithub.com" + path;
        QTest::newRow("userinfo") << "https://user@github.com" + path;
        QTest::newRow("port") << "https://github.com:8443" + path;
        QTest::newRow("query") << "https://github.com" + path + "?download=1";
        QTest::newRow("fragment") << "https://github.com" + path + "#asset";
        QTest::newRow("wrong-owner") << "https://github.com/another/chroma/releases/download/v0.3.0/" + portableName;
        QTest::newRow("wrong-tag") << "https://github.com/nikitasokolov01/chroma/releases/download/v0.2.0/" + portableName;
        QTest::newRow("wrong-filename") << "https://github.com" + path + ".exe";
        QTest::newRow("cdn-is-redirect-only") << "https://release-assets.githubusercontent.com" + path;
        QTest::newRow("relative") << path;
    }

    void rejectsUntrustedInitialAssetAddresses()
    {
        QFETCH(QString, url);
        auto package = asset("v0.3.0", portableName);
        package["browser_download_url"] = url;
        QVERIFY(!select({ replaceAsset(release("v0.3.0"), 0, package) }));
    }

    void downloadRedirectsStayOnTrustedHttpsHosts()
    {
        for (const QString& host :
             { QString("github.com"), QString("release-assets.githubusercontent.com"), QString("objects.githubusercontent.com") }) {
            QVERIFY(ChromaUpdate::allowedDownloadUrl(QUrl("https://" + host + "/asset?signature=abc&expiry=123")));
            QVERIFY(ChromaUpdate::allowedDownloadUrl(QUrl("https://" + host + ":443/asset")));
            QVERIFY(!ChromaUpdate::allowedDownloadUrl(QUrl("http://" + host + "/asset")));
            QVERIFY(!ChromaUpdate::allowedDownloadUrl(QUrl("https://" + host + ".evil.example/asset")));
            QVERIFY(!ChromaUpdate::allowedDownloadUrl(QUrl("https://user:password@" + host + "/asset")));
            QVERIFY(!ChromaUpdate::allowedDownloadUrl(QUrl("https://" + host + ":444/asset")));
            QVERIFY(!ChromaUpdate::allowedDownloadUrl(QUrl("https://" + host + "/asset#fragment")));
        }
        QVERIFY(!ChromaUpdate::allowedDownloadUrl(QUrl("file:///C:/asset.zip")));
        QVERIFY(!ChromaUpdate::allowedDownloadUrl(QUrl("https://api.github.com/asset")));
        QVERIFY(!ChromaUpdate::allowedDownloadUrl(QUrl()));
    }

    void rejectsInvalidReleaseMetadataAndAssetSizes()
    {
        const auto candidate = release("v0.3.0");
        for (const QString& field : { QString("draft"), QString("prerelease") }) {
            auto invalid = candidate;
            invalid.remove(field);
            QVERIFY(!select({ invalid }, true, true));
            invalid[field] = "false";
            QVERIFY(!select({ invalid }, true, true));
        }
        auto draft = candidate;
        draft["draft"] = true;
        QVERIFY(!select({ draft }, true, true));
        auto invalidTag = candidate;
        invalidTag["tag_name"] = "latest";
        QVERIFY(!select({ invalidTag }, true, true));
        for (int index : { 0, 2, 3 }) {
            auto file = candidate.value("assets").toArray()[index].toObject();
            for (qint64 size : { qint64(-1), qint64(0), 2LL * 1024 * 1024 * 1024 + 1 }) {
                file["size"] = size;
                QVERIFY(!select({ replaceAsset(candidate, index, file) }));
            }
        }
        QVERIFY(!select({ replaceAsset(candidate, 2, asset("v0.3.0", "SHA256SUMS.txt", 1024 * 1024 + 1)) }));
        QVERIFY(!select({ replaceAsset(candidate, 3, asset("v0.3.0", "package-manifest.json", 8 * 1024 * 1024 + 1)) }));
        auto maximumSizes = replaceAsset(candidate, 0, asset("v0.3.0", portableName, 2LL * 1024 * 1024 * 1024));
        maximumSizes = replaceAsset(maximumSizes, 2, asset("v0.3.0", "SHA256SUMS.txt", 1024 * 1024));
        maximumSizes = replaceAsset(maximumSizes, 3, asset("v0.3.0", "package-manifest.json", 8 * 1024 * 1024));
        QVERIFY(select({ maximumSizes }));
    }

    void acceptsExactChecksumAndNormalizesHex()
    {
        const QByteArray name = portableName.toUtf8();
        for (const QByteArray& separator : { QByteArray("  "), QByteArray(" *"), QByteArray("\t") }) {
            const auto hash = ChromaUpdate::checksumFor(digest.toUpper() + separator + name + "\r\n", portableName);
            QVERIFY(hash);
            QCOMPARE(*hash, digest);
        }
        const auto hash =
            ChromaUpdate::checksumFor("# release checksums\n" + QByteArray(64, 'b') + "  other.zip\n" + digest + "  " + name, portableName);
        QVERIFY(hash);
        QCOMPARE(*hash, digest);
    }

    void rejectsDuplicateInvalidOrMissingChecksums()
    {
        const auto line = digest + "  " + portableName.toUtf8() + '\n';
        QVERIFY(!ChromaUpdate::checksumFor(line + line, portableName));
        QVERIFY(!ChromaUpdate::checksumFor(line + QByteArray(64, 'b') + " *" + portableName.toUtf8(), portableName));
        for (const QByteArray& invalid : { QByteArray(63, 'a'), QByteArray(65, 'a'), QByteArray(64, 'g'), QByteArray("SHA256") })
            QVERIFY(!ChromaUpdate::checksumFor(invalid + "  " + portableName.toUtf8(), portableName));
        QVERIFY(!ChromaUpdate::checksumFor(digest + portableName.toUtf8(), portableName));
        QVERIFY(!ChromaUpdate::checksumFor(line, portableName.toUpper()));
        QVERIFY(!ChromaUpdate::checksumFor(line, installerName));
        QVERIFY(!ChromaUpdate::checksumFor(digest + "  subdir/" + portableName.toUtf8(), portableName));
        QVERIFY(!ChromaUpdate::checksumFor(digest + "  ../asset.zip", "../asset.zip"));
        QVERIFY(!ChromaUpdate::checksumFor(digest + "  subdir\\asset.zip", "subdir\\asset.zip"));
        QVERIFY(!ChromaUpdate::checksumFor({}, portableName));
        QVERIFY(!ChromaUpdate::checksumFor(QByteArray(1024 * 1024, '\n') + line, portableName));
    }
};

QTEST_GUILESS_MAIN(ChromaReleaseTest)
#include "ChromaRelease_test.moc"
