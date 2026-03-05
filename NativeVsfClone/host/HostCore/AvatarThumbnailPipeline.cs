namespace HostCore;

public sealed record AvatarThumbnailJob(string AvatarPath, string ThumbnailPath, bool Force);

public sealed record AvatarThumbnailState(
    string AvatarPath,
    string ThumbnailPath,
    string Status,
    string LastError);

public sealed class AvatarThumbnailPipeline
{
    private readonly Func<AvatarThumbnailJob, CancellationToken, Task<bool>> _runJobAsync;
    private readonly object _gate = new();
    private readonly Queue<AvatarThumbnailJob> _jobs = new();
    private readonly HashSet<string> _pendingPaths = new(StringComparer.OrdinalIgnoreCase);
    private bool _isWorkerRunning;

    public AvatarThumbnailPipeline(Func<AvatarThumbnailJob, CancellationToken, Task<bool>> runJobAsync)
    {
        _runJobAsync = runJobAsync ?? throw new ArgumentNullException(nameof(runJobAsync));
    }

    public event EventHandler<AvatarThumbnailState>? StateChanged;
    public event EventHandler<bool>? WorkerRunningChanged;

    public bool IsWorkerRunning
    {
        get
        {
            lock (_gate)
            {
                return _isWorkerRunning;
            }
        }
    }

    public bool Enqueue(string avatarPath, bool force)
    {
        var normalizedPath = avatarPath?.Trim() ?? string.Empty;
        if (string.IsNullOrWhiteSpace(normalizedPath) || !File.Exists(normalizedPath))
        {
            return false;
        }

        var thumbnailPath = AvatarThumbnailWorker.BuildThumbnailPath(normalizedPath);
        if (!force && File.Exists(thumbnailPath))
        {
            RaiseState(new AvatarThumbnailState(normalizedPath, thumbnailPath, "ready", string.Empty));
            return true;
        }

        var startWorker = false;
        lock (_gate)
        {
            if (_pendingPaths.Contains(normalizedPath))
            {
                return false;
            }

            _pendingPaths.Add(normalizedPath);
            _jobs.Enqueue(new AvatarThumbnailJob(normalizedPath, thumbnailPath, force));

            if (!_isWorkerRunning)
            {
                _isWorkerRunning = true;
                startWorker = true;
            }
        }

        RaiseState(new AvatarThumbnailState(normalizedPath, thumbnailPath, "pending", string.Empty));
        if (startWorker)
        {
            WorkerRunningChanged?.Invoke(this, true);
            _ = ProcessQueueAsync();
        }

        return true;
    }

    private async Task ProcessQueueAsync()
    {
        while (true)
        {
            AvatarThumbnailJob? job = null;
            lock (_gate)
            {
                if (_jobs.Count > 0)
                {
                    job = _jobs.Dequeue();
                }
            }

            if (job is null)
            {
                lock (_gate)
                {
                    _isWorkerRunning = false;
                }
                WorkerRunningChanged?.Invoke(this, false);
                return;
            }

            var success = false;
            try
            {
                success = await _runJobAsync(job, CancellationToken.None);
            }
            catch
            {
                success = false;
            }
            finally
            {
                lock (_gate)
                {
                    _pendingPaths.Remove(job.AvatarPath);
                }
            }

            if (success && File.Exists(job.ThumbnailPath))
            {
                RaiseState(new AvatarThumbnailState(job.AvatarPath, job.ThumbnailPath, "ready", string.Empty));
            }
            else
            {
                RaiseState(new AvatarThumbnailState(job.AvatarPath, job.ThumbnailPath, "failed", "thumbnail-worker-failed"));
            }
        }
    }

    private void RaiseState(AvatarThumbnailState state)
    {
        StateChanged?.Invoke(this, state);
    }
}
