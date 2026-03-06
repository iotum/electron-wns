# get-channel.ps1
#
# Obtains a WNS push notification channel URI for the current application and
# writes a single JSON object to stdout:
#
#   { "type": "channel", "uri": "<channel-uri>", "expiresAt": "<ISO-8601>" }
#
# On failure writes:
#   { "type": "error", "message": "<description>" }
# and exits with code 1.
#
# Requirements:
#   - Windows 10 / Windows Server 2019 or later
#   - The calling application must have a package identity (MSIX-packaged).

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Helper: convert a WinRT IAsyncOperation<T> to a .NET Task<T> and wait on it
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

try {
    Add-Type -AssemblyName System.Runtime.WindowsRuntime | Out-Null

    # Load WinRT push-notification types
    $channelManagerType = [Windows.Networking.PushNotifications.PushNotificationChannelManager,
        Windows.Networking.PushNotifications, ContentType=WindowsRuntime]
    $channelType = [Windows.Networking.PushNotifications.PushNotificationChannel,
        Windows.Networking.PushNotifications, ContentType=WindowsRuntime]

    # Request a channel (async → sync)
    $asyncOp   = $channelManagerType::CreatePushNotificationChannelForApplicationAsync()
    $channel   = Await-WinRTAsync -AsyncOperation $asyncOp -ResultType $channelType

    $result = [PSCustomObject]@{
        type      = 'channel'
        uri       = $channel.Uri
        expiresAt = $channel.ExpirationTime.ToUniversalTime().ToString('o')
    }
    Write-Output (ConvertTo-Json $result -Compress)
}
catch {
    $err = [PSCustomObject]@{
        type    = 'error'
        message = $_.Exception.Message
    }
    Write-Output (ConvertTo-Json $err -Compress)
    exit 1
}
