'use strict';

const statusEl = document.getElementById('status');

const chart = new Chart(document.getElementById('fftChart').getContext('2d'), {
    type: 'line',
    data: { datasets: [{ data: [], borderColor: '#0072BD', borderWidth: 1,
                         pointRadius: 0, fill: false, tension: 0 }] },
    options: {
        animation: false,
        responsive: true,
        maintainAspectRatio: false,
        parsing: false,
        plugins: { legend: { display: false } },
        scales: {
            x: { type: 'linear',
                 title: { display: true, text: 'Frequência (Hz)', color: '#333' },
                 ticks: { color: '#333' },
                 grid:  { color: '#e0e0e0' } },
            y: { title: { display: true, text: 'Potência', color: '#333' },
                 ticks: { color: '#333' },
                 grid:  { color: '#e0e0e0' },
                 beginAtZero: true }
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
