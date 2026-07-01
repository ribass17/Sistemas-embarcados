'use strict';

const statusEl = document.getElementById('status');

const chart = new Chart(document.getElementById('fftChart').getContext('2d'), {
    type: 'scatter',
    data: { datasets: [{ data: [], pointRadius: 2,
                         pointBackgroundColor: 'rgba(137, 180, 250, 0.9)',
                         pointBorderWidth: 0 }] },
    options: {
        animation: false,
        responsive: true,
        maintainAspectRatio: false,
        plugins: { legend: { display: false } },
        scales: {
            x: { title: { display: true, text: 'Hz',        color: '#6c7086' },
                 ticks: { color: '#6c7086' },
                 grid:  { color: 'rgba(255,255,255,0.05)' } },
            y: { title: { display: true, text: 'Magnitude', color: '#6c7086' },
                 ticks: { color: '#6c7086' },
                 grid:  { color: 'rgba(255,255,255,0.08)' },
                 beginAtZero: true, max: 65535 }
        }
    }
});

const ws = new WebSocket('ws://' + location.hostname + '/ws');
ws.binaryType = 'arraybuffer';

ws.onopen  = () => { statusEl.textContent = 'Conectado...'; };
ws.onclose = () => { statusEl.textContent = 'Desconectado.'; };
ws.onerror = () => { statusEl.textContent = 'Erro WebSocket.'; };

// pts é alocado uma vez no primeiro frame e reutilizado — sem GC pressure
let pts = null;

ws.onmessage = (evt) => {
    const sr  = new DataView(evt.data).getUint32(0, true);
    const mag = new Uint16Array(evt.data, 4);
    const hz  = sr / (2 * mag.length);

    if (!pts) {
        pts = Array.from(mag, () => ({ x: 0, y: 0 }));
        chart.data.datasets[0].data = pts;
    }

    for (let i = 0; i < mag.length; i++) {
        pts[i].x = Math.round(i * hz);
        pts[i].y = mag[i];
    }
    chart.update('none');
    statusEl.textContent = `sr=${sr} Hz | ${new Date().toLocaleTimeString()}`;
};
