namespace HostCore;

public sealed class OutputService
{
    public NcResultCode StartSpout(uint width, uint height, uint fps, string channelName)
    {
        var options = new NcSpoutOptions
        {
            Width = width,
            Height = height,
            Fps = fps,
            ChannelName = channelName,
        };
        return NativeCoreInterop.nc_start_spout(ref options);
    }

    public NcResultCode StopSpout() => NativeCoreInterop.nc_stop_spout();

    public NcResultCode StartOsc(ushort bindPort, string publishAddress)
    {
        var options = new NcOscOptions
        {
            BindPort = bindPort,
            PublishAddress = publishAddress,
        };
        return NativeCoreInterop.nc_start_osc(ref options);
    }

    public NcResultCode StopOsc() => NativeCoreInterop.nc_stop_osc();
}
