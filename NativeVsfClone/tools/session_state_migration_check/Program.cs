using HostCore;
using System.Text;

static void AssertOrThrow(bool condition, string message)
{
    if (!condition)
    {
        throw new InvalidOperationException(message);
    }
}

var argsMap = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
for (var i = 0; i < args.Length - 1; i += 2)
{
    if (args[i].StartsWith("--", StringComparison.Ordinal))
    {
        argsMap[args[i]] = args[i + 1];
    }
}

var summaryPath = argsMap.TryGetValue("--summary", out var summary) && !string.IsNullOrWhiteSpace(summary)
    ? summary
    : Path.Combine("build", "reports", "session_state_migration_check_summary.txt");

var tmpRoot = Path.Combine(Path.GetTempPath(), $"vsfclone_session_migration_{Guid.NewGuid():N}");
Directory.CreateDirectory(tmpRoot);

var lines = new List<string>
{
    "Session State Migration Check Summary",
    $"Generated: {DateTimeOffset.UtcNow:O}",
};

var allPass = true;
try
{
    var v1Path = Path.Combine(tmpRoot, "session_v1.json");
    File.WriteAllText(
        v1Path,
        """
        {
          "Version": 1,
          "AvatarPath": "D:\\sample\\legacy.vrm",
          "SpoutChannelName": "LegacyChannel",
          "OscBindPort": 39000,
          "OscPublishAddress": "127.0.0.1:39001",
          "Sidecar": {
            "ParserMode": "sidecar",
            "SidecarPath": "",
            "TimeoutMs": 15000,
            "StrictMode": false
          },
          "LastProfileName": "quality",
          "LastUpdatedUtc": "2025-01-01T00:00:00+00:00"
        }
        """,
        Encoding.UTF8);

    var storeV1 = new SessionStateStore(v1Path);
    var modelV1 = storeV1.Load();
    AssertOrThrow(modelV1.Version >= 5, "v1 migration failed: Version not upgraded to >=5");
    AssertOrThrow(modelV1.Tracking.ListenPort == 49983, "v1 migration failed: default tracking port mismatch");
    AssertOrThrow(modelV1.Tracking.StaleTimeoutMs == 500, "v1 migration failed: default stale timeout mismatch");
    AssertOrThrow(modelV1.Tracking.SourceType == TrackingSourceType.OscIfacial, "v1 migration failed: default source type mismatch");
    AssertOrThrow(modelV1.UiMode == "beginner", "v1 migration failed: default ui mode mismatch");
    lines.Add("- case1_v1_to_v5: PASS");

    var invalidPath = Path.Combine(tmpRoot, "session_v5_invalid.json");
    File.WriteAllText(
        invalidPath,
        """
        {
          "Version": 5,
          "AvatarPath": "",
          "SpoutChannelName": "X",
          "OscBindPort": 1,
          "OscPublishAddress": "127.0.0.1:1",
          "Sidecar": {
            "ParserMode": "sidecar",
            "SidecarPath": "",
            "TimeoutMs": 15000,
            "StrictMode": false
          },
          "Tracking": {
            "ListenPort": 0,
            "StaleTimeoutMs": 1,
            "LastActive": false,
            "SourceType": 99,
            "CameraDeviceKey": null,
            "InferenceFpsCap": 9999
          },
          "LastProfileName": "quality",
          "UiMode": "invalid-value",
          "LastUpdatedUtc": "0001-01-01T00:00:00+00:00"
        }
        """,
        Encoding.UTF8);

    var storeInvalid = new SessionStateStore(invalidPath);
    var modelInvalid = storeInvalid.Load();
    AssertOrThrow(modelInvalid.Tracking.ListenPort == 49983, "normalize failed: tracking port");
    AssertOrThrow(modelInvalid.Tracking.StaleTimeoutMs == 50, "normalize failed: stale timeout clamp");
    AssertOrThrow(modelInvalid.Tracking.SourceType == TrackingSourceType.OscIfacial, "normalize failed: source type clamp");
    AssertOrThrow(modelInvalid.Tracking.InferenceFpsCap == 120, "normalize failed: fps cap clamp");
    AssertOrThrow(modelInvalid.UiMode == "beginner", "normalize failed: ui mode");
    AssertOrThrow(modelInvalid.LastUpdatedUtc != DateTimeOffset.MinValue, "normalize failed: last updated default");
    lines.Add("- case2_invalid_v5_normalization: PASS");

    var roundtripPath = Path.Combine(tmpRoot, "session_roundtrip.json");
    var storeRoundtrip = new SessionStateStore(roundtripPath);
    storeRoundtrip.Save(modelInvalid);
    var loadedRoundtrip = storeRoundtrip.Load();
    AssertOrThrow(loadedRoundtrip.Version >= 5, "roundtrip failed: version");
    AssertOrThrow(loadedRoundtrip.Tracking.ListenPort == 49983, "roundtrip failed: tracking port");
    AssertOrThrow(loadedRoundtrip.UiMode == "beginner", "roundtrip failed: ui mode");
    lines.Add("- case3_roundtrip: PASS");
}
catch (Exception ex)
{
    allPass = false;
    lines.Add($"- failure: {ex.Message}");
}
finally
{
    try
    {
        Directory.Delete(tmpRoot, recursive: true);
    }
    catch
    {
        // ignore cleanup failures
    }
}

lines.Insert(2, $"Overall: {(allPass ? "PASS" : "FAIL")}");

var summaryDir = Path.GetDirectoryName(summaryPath);
if (!string.IsNullOrWhiteSpace(summaryDir))
{
    Directory.CreateDirectory(summaryDir);
}
File.WriteAllLines(summaryPath, lines, Encoding.UTF8);
Console.WriteLine($"summary={Path.GetFullPath(summaryPath)}");

return allPass ? 0 : 1;
