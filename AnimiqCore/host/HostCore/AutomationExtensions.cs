namespace HostCore;

public interface IAutomationExtension
{
    string Id { get; }
    void Execute(IReadOnlyDictionary<string, string> parameters);
}

public sealed class AutomationExtensionRegistry
{
    private readonly Dictionary<string, IAutomationExtension> _extensions = new(StringComparer.OrdinalIgnoreCase);

    public void Register(IAutomationExtension extension)
    {
        if (extension is null || string.IsNullOrWhiteSpace(extension.Id))
        {
            return;
        }
        _extensions[extension.Id.Trim()] = extension;
    }

    public bool TryExecute(string extensionId, IReadOnlyDictionary<string, string> parameters)
    {
        if (string.IsNullOrWhiteSpace(extensionId))
        {
            return false;
        }

        if (!_extensions.TryGetValue(extensionId.Trim(), out var extension))
        {
            return false;
        }

        extension.Execute(parameters);
        return true;
    }

    public IReadOnlyCollection<string> ListIds()
    {
        return _extensions.Keys.ToArray();
    }
}
