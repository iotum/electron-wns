[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [Alias('Sid')]
    [string]$ClientId,

    [Parameter(Mandatory = $true)]
    [string]$TenantId,

    [Parameter(Mandatory = $true)]
    [string]$Secret,

    [Parameter(Mandatory = $true)]
    [string]$Channel,

    [Parameter(Mandatory = $true)]
    [string]$Message,

    [int]$TimeoutSec = 20,

    [int]$Ttl = 300,

    [ValidateSet('cache', 'no-cache')]
    [string]$CachePolicy
)

$ErrorActionPreference = 'Stop'

function ConvertTo-FormUrlEncoded {
    param(
        [Parameter(Mandatory = $true)]
        [hashtable]$Data
    )

    ($Data.GetEnumerator() | ForEach-Object {
        "{0}={1}" -f [Uri]::EscapeDataString([string]$_.Key), [Uri]::EscapeDataString([string]$_.Value)
    }) -join '&'
}

function Get-WnsAccessToken {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ClientId,

        [Parameter(Mandatory = $true)]
        [string]$TenantId,

        [Parameter(Mandatory = $true)]
        [string]$ClientSecret,

        [Parameter(Mandatory = $true)]
        [int]$Timeout
    )

    $tokenUri = "https://login.microsoftonline.com/$TenantId/oauth2/v2.0/token"
    $scope = 'https://wns.windows.com/.default'
    $formBody = ConvertTo-FormUrlEncoded -Data @{
        grant_type    = 'client_credentials'
        client_id     = $ClientId
        client_secret = $ClientSecret
        scope         = $scope
    }

    Write-Verbose "[TOKEN] POST $tokenUri"
    Write-Verbose "[TOKEN] scope=$scope client_id length=$($ClientId.Length)"

    $response = Invoke-RestMethod -Uri $tokenUri -Method Post -Body $formBody -ContentType 'application/x-www-form-urlencoded' -TimeoutSec $Timeout

    if (-not $response.access_token) {
        throw "Token response missing access_token. Raw response: $($response | ConvertTo-Json -Depth 6)"
    }

    Write-Verbose "[TOKEN] Access token acquired. token_type=$($response.token_type) expires_in=$($response.expires_in)"
    return [string]$response.access_token
}

function Send-WnsRawPush {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ChannelUri,

        [Parameter(Mandatory = $true)]
        [string]$AccessToken,

        [Parameter(Mandatory = $true)]
        [string]$Payload,

        [Parameter(Mandatory = $true)]
        [int]$Timeout,

        [Parameter(Mandatory = $true)]
        [int]$TimeToLive,

        [string]$WnsCachePolicy
    )

    if (-not [Uri]::IsWellFormedUriString($ChannelUri, [System.UriKind]::Absolute)) {
        throw "Invalid channel URI: $ChannelUri"
    }

    if (-not $ChannelUri.StartsWith('https://', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "WNS channel URI must be HTTPS. Provided: $ChannelUri"
    }

    $headers = @{
        Authorization          = "Bearer $AccessToken"
        'X-WNS-Type'          = 'wns/raw'
        'Content-Type'        = 'application/octet-stream'
    }

    $headers['X-WNS-TTL'] = [string]$TimeToLive

    if ($WnsCachePolicy) {
        $headers['X-WNS-Cache-Policy'] = $WnsCachePolicy
    }

    Write-Verbose "[PUSH] POST $ChannelUri"
    Write-Verbose "[PUSH] Payload length: $($Payload.Length) chars"
    Write-Verbose "[PUSH] Headers: $((($headers.GetEnumerator() | Sort-Object Name | ForEach-Object { '{0}={1}' -f $_.Name, $_.Value }) -join '; '))"

    $webResponse = $null
    try {
        $webResponse = Invoke-WebRequest -Uri $ChannelUri -Method Post -Headers $headers -Body ([System.Text.Encoding]::UTF8.GetBytes($Payload)) -TimeoutSec $Timeout
    }
    catch {
        if ($_.Exception.Response -is [System.Net.HttpWebResponse]) {
            $failedResponse = [System.Net.HttpWebResponse]$_.Exception.Response
            $statusCode = [int]$failedResponse.StatusCode
            $statusDescription = $failedResponse.StatusDescription

            Write-Host "HTTP $statusCode $statusDescription" -ForegroundColor Yellow

            foreach ($headerName in @('X-WNS-NotificationStatus', 'X-WNS-DeviceConnectionStatus', 'X-WNS-Error-Description', 'X-WNS-Debug-Trace', 'X-WNS-Msg-ID')) {
                $headerValue = $failedResponse.Headers[$headerName]
                if ($headerValue) {
                    Write-Host ("{0}: {1}" -f $headerName, $headerValue)
                }
            }

            $reader = New-Object System.IO.StreamReader($failedResponse.GetResponseStream())
            $errorBody = $reader.ReadToEnd()
            $reader.Dispose()

            if ($errorBody) {
                Write-Host "Body:" -ForegroundColor Yellow
                Write-Host $errorBody
            }

            throw "WNS push failed with HTTP $statusCode $statusDescription"
        }

        throw
    }

    $status = [int]$webResponse.StatusCode
    $description = $webResponse.StatusDescription

    Write-Host "HTTP $status $description" -ForegroundColor Green

    foreach ($headerName in @('X-WNS-NotificationStatus', 'X-WNS-DeviceConnectionStatus', 'X-WNS-Error-Description', 'X-WNS-Debug-Trace', 'X-WNS-Msg-ID')) {
        $headerValue = $webResponse.Headers[$headerName]
        if ($headerValue) {
            Write-Host ("{0}: {1}" -f $headerName, $headerValue)
        }
    }

    if ($webResponse.Content) {
        Write-Verbose "[PUSH] Response body: $($webResponse.Content)"
    }

    return $webResponse
}

Write-Verbose '[START] Sending WNS raw push'

$token = Get-WnsAccessToken -ClientId $ClientId -TenantId $TenantId -ClientSecret $Secret -Timeout $TimeoutSec
$response = Send-WnsRawPush -ChannelUri $Channel -AccessToken $token -Payload $Message -Timeout $TimeoutSec -TimeToLive $Ttl -WnsCachePolicy $CachePolicy

Write-Verbose "[DONE] Push complete. HTTP $([int]$response.StatusCode)"
