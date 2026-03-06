# wns-listener.ps1
#
# Registers a WNS push notification channel for the current application,
# emits the channel details, and then waits for push notifications.
#
# Every event is written to stdout as a compact JSON object followed by a
# newline so that the Node.js parent process can parse it incrementally.
#
# Message types:
#   { "type": "channel",      "uri": "...", "expiresAt": "..." }
#   { "type": "notification", "notificationType": "...", "payload": "...", "timestamp": "..." }
#   { "type": "error",        "message": "..." }
#
# The script runs until the parent process terminates it (SIGTERM / kill).
#
# Requirements:
#   - Windows 10 / Windows Server 2019 or later
#   - The calling application must have a package identity (MSIX-packaged).

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Helper: convert a WinRT IAsyncOperation<T> to a .NET Task<T> and wait
# ---------------------------------------------------------------------------
function Await-WinRTAsync {
    param(
        [Parameter(Mandatory)] $AsyncOperation,
        [Parameter(Mandatory)] [Type] $ResultType
    )
    Add-Type -AssemblyName System.Runtime.WindowsRuntime | Out-Null
    $extensionMethod = [System.WindowsRuntimeSystemExtensions].GetMethods() |
        Where-Object {
            $_.Name -eq 'AsTask' -and
            $_.GetParameters().Count -eq 1 -and
            $_.IsGenericMethod
        } |
        Select-Object -First 1
    $task = $extensionMethod.MakeGenericMethod($ResultType).Invoke($null, @($AsyncOperation))
    $task.Wait() | Out-Null
    return $task.Result
}

# ---------------------------------------------------------------------------
# Write a JSON message to stdout (single line)
# ---------------------------------------------------------------------------
function Write-JsonMessage {
    param([hashtable] $Data)
    [Console]::Out.WriteLine((ConvertTo-Json ([PSCustomObject]$Data) -Compress -Depth 5))
    [Console]::Out.Flush()
}

try {
    Add-Type -AssemblyName System.Runtime.WindowsRuntime | Out-Null

    $channelManagerType = [Windows.Networking.PushNotifications.PushNotificationChannelManager,
        Windows.Networking.PushNotifications, ContentType=WindowsRuntime]
    $channelType = [Windows.Networking.PushNotifications.PushNotificationChannel,
        Windows.Networking.PushNotifications, ContentType=WindowsRuntime]

    # Obtain the channel
    $asyncOp = $channelManagerType::CreatePushNotificationChannelForApplicationAsync()
    $channel = Await-WinRTAsync -AsyncOperation $asyncOp -ResultType $channelType

    # Report the channel URI to the Node.js process
    Write-JsonMessage @{
        type      = 'channel'
        uri       = $channel.Uri
        expiresAt = $channel.ExpirationTime.ToUniversalTime().ToString('o')
    }

    # ---------------------------------------------------------------------------
    # Subscribe to incoming push notifications
    # ---------------------------------------------------------------------------
    # A thread-safe queue shared between the WinRT event callback and the
    # polling loop below.
    $queue = [System.Collections.Concurrent.ConcurrentQueue[hashtable]]::new()

    $handlerBlock = {
        param($sender, $eventArgs)
        $n = $eventArgs.Notification
        $notifType = $n.NotificationType.ToString().ToLower()
        $payload = ''
        if ($notifType -eq 'raw') {
            $payload = $n.RawNotification.Content
        }
        elseif ($notifType -eq 'toast') {
            $payload = $n.ToastNotification.Content.GetXml()
        }
        elseif ($notifType -eq 'tile') {
            $payload = $n.TileNotification.Content.GetXml()
        }
        elseif ($notifType -eq 'badge') {
            $payload = $n.BadgeNotification.Content.GetXml()
        }

        # Mark the notification as handled so Windows does not show a system UI
        $eventArgs.Cancel = $true

        $queue.Enqueue(@{
            type             = 'notification'
            notificationType = $notifType
            payload          = $payload
            timestamp        = [DateTime]::UtcNow.ToString('o')
        })
    }

    $receivedType = [Windows.Foundation.TypedEventHandler``2].MakeGenericType(
        $channelType,
        [Windows.Networking.PushNotifications.PushNotificationReceivedEventArgs,
            Windows.Networking.PushNotifications, ContentType=WindowsRuntime]
    )
    $delegate = [Delegate]::CreateDelegate($receivedType, $handlerBlock.GetNewClosure(), 'Invoke')
    $channel.add_PushNotificationReceived($delegate)

    # ---------------------------------------------------------------------------
    # Poll the queue until the process is terminated
    # ---------------------------------------------------------------------------
    while ($true) {
        $item = $null
        while ($queue.TryDequeue([ref] $item)) {
            Write-JsonMessage $item
        }
        Start-Sleep -Milliseconds 200
    }
}
catch {
    Write-JsonMessage @{
        type    = 'error'
        message = $_.Exception.Message
    }
    exit 1
}
