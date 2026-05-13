#ifndef INDEX_H
#define INDEX_H

#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
    <meta charset='UTF-8'>
    <title>Smart Parking Dashboard</title>
    <style>
        body{font-family:sans-serif; background:#f0f2f5; padding:20px; text-align:center;}
        .card{background:white; border-radius:10px; padding:15px; margin:10px auto; max-width:900px; box-shadow:0 4px 6px rgba(0,0,0,0.1); border-top: 5px solid #4CAF50;}
        .remote-card{border-top: 5px solid #2196F3;}
        table{width:100%; border-collapse:collapse; margin-top:10px;} 
        th,td{padding:12px; border-bottom:1px solid #eee;}
        th{background:#f8f9fa;} 
        .status-ok{background:#d4edda; color:#155724; border-radius:4px; padding:4px;}
        .status-occ{background:#f8d7da; color:#721c24; border-radius:4px; padding:4px; font-weight:bold;}
        .alert-row{background:#fff3cd !important; font-weight:bold; color:#856404;}
        #loader { color: #666; font-size: 0.9em; margin-bottom: 15px; }
    </style>
</head>
<body>
    <h1>🅿️ Smart Parking Dashboard (Live)</h1>
    <div id="loader">Connessione ai sensori in corso...</div>
    <div id="dashboard"></div>

    <script>
        async function updateDashboard() {
            try {
                const response = await fetch('/data');
                const data = await response.json();
                let html = '';

                // --- FILA LOCALE ---
                html += renderCard("Fila A (Locale)", data.local.sensors, data.local.temp, data.local.pres, data.local.hum, false);

                // --- FILE REMOTE ---
                for (const rowName in data.remote) {
                    const row = data.remote[rowName];
                    html += renderCard("Fila " + rowName, row.sensors, null, null, null, true);
                }

                document.getElementById('dashboard').innerHTML = html;
                document.getElementById('loader').innerText = "Ultimo aggiornamento: " + new Date().toLocaleTimeString();
            } catch (err) {
                document.getElementById('loader').innerText = "Errore di connessione!";
                console.error("Errore fetch:", err);
            }
        }

        function renderCard(title, sensors, t, p, h, isRemote) {
            let cardClass = isRemote ? "card remote-card" : "card";
            let html = `<div class="${cardClass}"><h2>${title}</h2><table>
                        <tr><th>Posto</th><th>Distanza</th><th>Stato</th><th>Meteo Ambientale</th></tr>`;
            
            sensors.forEach((s) => {
                let rowClass = s.alert ? "class='alert-row'" : "";
                let statusClass = s.parked ? "status-occ" : "status-ok";
                
                // Logica N/A
                let distDisplay = (s.dist === -1) ? "N/A" : s.dist + " cm";
                let statusText = (s.dist === -1) ? "---" : (s.parked ? "OCCUPATO" : "LIBERO");
                
                let meteoStr = isRemote ? `T: ${s.temp}°C | P: ${s.pres} | U: ${s.hum}%` : `T: ${t}°C | P: ${p} | U: ${h}%`;
                
                html += `<tr ${rowClass}>
                            <td>Posto ${s.label}</td>
                            <td>${distDisplay} ${s.dist_err ? '❓' : ''}</td>
                            <td><span class="${statusClass}">${statusText}</span></td>
                            <td>${meteoStr} ${s.alert ? '⚠️' : ''}</td>
                         </tr>`;
            });
            html += `</table></div>`;
            return html;
        }

        setInterval(updateDashboard, 3000); 
        updateDashboard(); 
    </script>
</body>
</html>
)=====";

#endif