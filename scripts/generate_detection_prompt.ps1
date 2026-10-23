param(
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [string]$Phrase = '开始检测'
)
# OneCore Kangkang requires PowerShell 7 here; import the WAV into WSL after generation.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech
$voice = [System.Speech.Synthesis.SpeechSynthesizer]::new()
try {
    $voice.SelectVoice('Microsoft Kangkang')
    $voice.Rate = -1
    $voice.Volume = 100
    $format = [System.Speech.AudioFormat.SpeechAudioFormatInfo]::new(
        16000,
        [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,
        [System.Speech.AudioFormat.AudioChannel]::Mono
    )
    $voice.SetOutputToWaveFile($OutputPath, $format)
    $voice.Speak($Phrase)
    $voice.SetOutputToNull()
} finally {
    $voice.Dispose()
}
