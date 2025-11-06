# PowerShell test runner for agent + mocks
param()
Set-StrictMode -Version Latest
Push-Location -Path (Join-Path $PSScriptRoot "..")

Write-Host "Building images: nlu, api_mock, agent"
docker compose build nlu api_mock agent

Write-Host "Starting services"
docker compose up -d nlu api_mock agent
Start-Sleep -Seconds 2

Write-Host "Sending test message: agregarItem"
Invoke-RestMethod -Method Post -Uri http://localhost:8081/whatsapp_hook -Body (ConvertTo-Json @{ text = 'Quiero agregar una pizza' }) -ContentType 'application/json' -ErrorAction SilentlyContinue

Write-Host "Sending test message: consultarMenu"
Invoke-RestMethod -Method Post -Uri http://localhost:8081/whatsapp_hook -Body (ConvertTo-Json @{ text = 'Muéstrame el menú' }) -ContentType 'application/json' -ErrorAction SilentlyContinue

Write-Host "Sending test message: desconocido"
Invoke-RestMethod -Method Post -Uri http://localhost:8081/whatsapp_hook -Body (ConvertTo-Json @{ text = 'Hola qué tal' }) -ContentType 'application/json' -ErrorAction SilentlyContinue

Write-Host "Agent logs (last 200 lines):"
docker compose logs --no-color --tail=200 agent

Write-Host "Done. To stop: docker compose down"
Pop-Location
