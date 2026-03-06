using HostCore;
using System.Net.Sockets;
using System.Text;

static string GetArg(string[] args, string key, string fallback)
{
    for (var i = 0; i < args.Length - 1; i++)
    {
        if (string.Equals(args[i], key, StringComparison.OrdinalIgnoreCase))
        {
            return args[i + 1];
        }
    }
    return fallback;
}

var listenPort = ushort.Parse(GetArg(args, "--port", "50983"));
var packetCount = int.Parse(GetArg(args, "--packet-count", "500"));
var maxDurationSec = int.Parse(GetArg(args, "--max-seconds", "8"));
var summaryPath = GetArg(args, "--summary", Path.Combine("build", "reports", "tracking_parser_fuzz_gate_summary.txt"));

var service = new TrackingInputService();
var startRc = service.Start(new TrackingStartOptions(listenPort, 500, TrackingSourceType.OscIfacial, string.Empty, 30));
if (startRc != NcResultCode.Ok)
{
    throw new InvalidOperationException($"TrackingInputService.Start failed: {startRc}");
}

var seed = Environment.TickCount;
var random = new Random(seed);
var sent = 0;
var started = DateTimeOffset.UtcNow;
var ok = true;
var failure = string.Empty;

try
{
    using var udp = new UdpClient();
    for (var i = 0; i < packetCount; i++)
    {
        var len = random.Next(1, 256);
        var buffer = new byte[len];
        random.NextBytes(buffer);
        _ = await udp.SendAsync(buffer, buffer.Length, "127.0.0.1", listenPort);
        sent++;
    }

    await Task.Delay(300);
    var deadline = DateTimeOffset.UtcNow.AddSeconds(maxDurationSec);
    while (DateTimeOffset.UtcNow < deadline)
    {
        await Task.Delay(100);
        var d = service.GetDiagnostics();
        if (d.ReceivedPackets > 0 || d.ParseErrors > 0)
        {
            break;
        }
    }

    var final = service.GetDiagnostics();
    if (!final.IsActive)
    {
        ok = false;
        failure = "service not active after fuzz run";
    }
    if (final.ReceivedPackets + final.ParseErrors == 0)
    {
        ok = false;
        failure = "no packet/parse signal observed after sending fuzz packets";
    }
}
catch (Exception ex)
{
    ok = false;
    failure = ex.Message;
}
finally
{
    _ = service.Stop();
}

var diag = service.GetDiagnostics();
var duration = (DateTimeOffset.UtcNow - started).TotalSeconds;

var lines = new List<string>
{
    "Tracking Parser Fuzz Gate Summary",
    $"Generated: {DateTimeOffset.UtcNow:O}",
    $"ListenPort: {listenPort}",
    $"Seed: {seed}",
    $"PacketCountRequested: {packetCount}",
    $"PacketCountSent: {sent}",
    $"DurationSec: {duration:F3}",
    "",
    "Diagnostics",
    $"- IsActive: {diag.IsActive}",
    $"- DetectedFormat: {diag.DetectedFormat}",
    $"- InputFps: {diag.InputFps:F3}",
    $"- LastPacketAgeMs: {diag.LastPacketAgeMs}",
    $"- IsStale: {diag.IsStale}",
    $"- ReceivedPackets: {diag.ReceivedPackets}",
    $"- DroppedPackets: {diag.DroppedPackets}",
    $"- ParseErrors: {diag.ParseErrors}",
    $"- SourceStatus: {diag.SourceStatus}",
    "",
    "Gate Overall",
    $"- Overall: {(ok ? "PASS" : "FAIL")}",
};
if (!string.IsNullOrWhiteSpace(failure))
{
    lines.Add($"- Failure: {failure}");
}

var summaryDir = Path.GetDirectoryName(summaryPath);
if (!string.IsNullOrWhiteSpace(summaryDir))
{
    Directory.CreateDirectory(summaryDir);
}
await File.WriteAllLinesAsync(summaryPath, lines, Encoding.UTF8);
Console.WriteLine($"summary={Path.GetFullPath(summaryPath)}");
Environment.ExitCode = ok ? 0 : 1;
