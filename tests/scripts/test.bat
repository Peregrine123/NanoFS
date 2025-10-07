# 获取当前用户的 Path 和系统的 Path
 $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
 $systemPath = [Environment]::GetEnvironmentVariable('Path', 'Machine')
# 合并成一个完整的路径列表
 $allPaths = ($userPath + ';' + $systemPath) -split ';' | Where-Object { $_ -ne '' }

Write-Host "=== 检查重复的路径 ==="
 $duplicates = $allPaths | Group-Object | Where-Object { $_.Count -gt 1 }
if ($duplicates) {
    $duplicates | ForEach-Object {
        Write-Warning "发现重复路径: $($_.Name) (重复 $($_.Count) 次)"
    }
} else {
    Write-Host "未发现重复路径。" -ForegroundColor Green
}

Write-Host "`n=== 检查无效的路径 ==="
 $invalidPaths = @()
foreach ($path in $allPaths) {
    if (-not (Test-Path $path)) {
        $invalidPaths += $path
    }
}

if ($invalidPaths.Count -gt 0) {
    Write-Warning "发现 $($invalidPaths.Count) 个无效路径："
    $invalidPaths | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
} else {
    Write-Host "所有路径均有效。" -ForegroundColor Green
}

Write-Host "`n=== PATH 总长度 ==="
 $totalLength = ($userPath + ';' + $systemPath).Length
Write-Host "当前 PATH 变量总长度: $totalLength 字符"
Write-Host "Windows 环境变量长度限制约为 2048 字符。"
if ($totalLength -gt 1800) {
    Write-Warning "长度已接近限制，建议进行清理！"
}