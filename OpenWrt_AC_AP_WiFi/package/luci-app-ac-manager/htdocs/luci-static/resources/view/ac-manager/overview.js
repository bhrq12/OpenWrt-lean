'use strict';
'require view';
'require dom';
'require ui';
'require rpc';
'require poll';
'require fs';

var callServiceStatus = rpc.declare({
	object: 'ac-manager.status',
	method: 'get',
	expect: { '': {} }
});

function getServiceName(name) {
	var s = String(name || '').split(/\s+/);
	if (s[s.length - 1] == 'ac-manager')
		s[s.length - 1] = _('AC Manager');
	else
		s[s.length - 1] = _('AC Manager');
	return s.join(' ');
}

function renderStatusCard(data) {
	var running = (data.running === true || data.running === 1 || data.running === '1');
	var card = E('div', { 'class': 'cbi-section' });
	var color = running ? '#4caf50' : '#f44336';
	var statusText = running ? _('Running') : _('Stopped');
	var icon = running ? '✓' : '✗';

	card.appendChild(E('h3', { 'style': 'color:' + color },
		icon + ' ' + statusText + ' — ' + _('AC Controller')));

	var info = E('table', { 'class': 'table' });
	info.appendChild(E('tr', {},
		E('td', { 'style': 'width:30%' }, E('strong', {}, _('PID:'))),
		E('td', {}, data.pid ? String(data.pid) : '-')));
	info.appendChild(E('tr', {},
		E('td', {}, E('strong', {}, _('Version:'))),
		E('td', {}, data.version || '-')));
	info.appendChild(E('tr', {},
		E('td', {}, E('strong', {}, _('Uptime:'))),
		E('td', {}, data.uptime ? String(data.uptime) + 's' : '-')));
	info.appendChild(E('tr', {},
		E('td', {}, E('strong', {}, _('WebSocket Port:'))),
		E('td', {}, data.ws_port ? String(data.ws_port) : '-')));
	info.appendChild(E('tr', {},
		E('td', {}, E('strong', {}, _('WS Connections:'))),
		E('td', {}, data.ws_connections != null ? String(data.ws_connections) : '-')));
	info.appendChild(E('tr', {},
		E('td', {}, E('strong', {}, _('Online APs:'))),
		E('td', {}, data.online_devices != null ? String(data.online_devices) : '-')));
	info.appendChild(E('tr', {},
		E('td', {}, E('strong', {}, _('Total APs:'))),
		E('td', {}, data.total_devices != null ? String(data.total_devices) : '-')));
	card.appendChild(info);

	return card;
}

function handleServiceAction(action) {
	return fs.exec_direct('/etc/init.d/ac-manager', [action])
		.then(function() {
			ui.addNotification(null, E('p', _('Service %s request sent.').format(action)), 'info');
		})
		.catch(function(err) {
			ui.addNotification(null, E('p', _('Failed to %s service: %s').format(action, err.message)), 'error');
		});
}

return view.extend({
	load: function() {
		return Promise.all([
			L.resolveDefault(callServiceStatus(), {})
		]);
	},

	render: function(data) {
		var status = data[0] || {};
		var container = E('div', { 'class': 'cbi-map' });

		container.appendChild(E('h2', {}, _('AC Manager - Overview')));

		var actions = E('div', { 'class': 'cbi-section' });
		var btnStart = E('button', {
			'class': 'cbi-button cbi-button-apply',
			'click': function() { return handleServiceAction('start'); }
		}, _('Start'));
		var btnStop = E('button', {
			'class': 'cbi-button cbi-button-reset',
			'click': function() { return handleServiceAction('stop'); }
		}, _('Stop'));
		var btnRestart = E('button', {
			'class': 'cbi-button cbi-button-reload',
			'click': function() { return handleServiceAction('restart'); }
		}, _('Restart'));
		var running = (status.running === true || status.running === 1 || status.running === '1');
		if (running) {
			btnStart.disabled = true;
		} else {
			btnStop.disabled = true;
			btnRestart.disabled = true;
		}
		actions.appendChild(E('h3', {}, _('Service Control')));
		var btnRow = E('div', { 'style': 'display:flex;gap:8px' });
		btnRow.appendChild(btnStart);
		btnRow.appendChild(btnStop);
		btnRow.appendChild(btnRestart);
		actions.appendChild(btnRow);
		container.appendChild(actions);

		container.appendChild(renderStatusCard(status));

		var logSection = E('div', { 'class': 'cbi-section' });
		logSection.appendChild(E('h3', {}, _('Recent Logs')));
		var logTable = E('table', { 'class': 'table', 'id': 'recent-logs' });
		logTable.appendChild(E('tr', {},
			E('th', {}, _('Timestamp')),
			E('th', {}, _('Level')),
			E('th', {}, _('Module')),
			E('th', {}, _('Message'))
		));
		logSection.appendChild(logTable);
		container.appendChild(logSection);

		poll.add(function() {
			return L.resolveDefault(callServiceStatus(), {})
				.then(function(s) {
					var oldCard = container.querySelector('.cbi-section:nth-of-type(2)');
					if (oldCard) {
						var newCard = renderStatusCard(s || {});
						oldCard.parentNode.replaceChild(newCard, oldCard);
					}
					return L.resolveDefault(fs.read_direct('/var/log/ac-manager.log', 4096), '')
						.then(function(logdata) {
							var tbody = logTable;
							var rows = String(logdata).trim().split('\n').slice(-10).reverse();
							while (tbody.rows.length > 1) tbody.deleteRow(1);
							rows.forEach(function(line) {
								if (!line) return;
								var parts = line.match(/(\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}:\d{2})\s+(\w+)\s+(\w+)\s+(.*)/);
								var row = tbody.insertRow(-1);
								if (parts) {
									row.insertCell(0).textContent = parts[1];
									row.insertCell(1).textContent = parts[2];
									row.insertCell(2).textContent = parts[3];
									row.insertCell(3).textContent = parts[4];
								} else {
									row.insertCell(0).textContent = '';
									row.insertCell(1).textContent = '';
									row.insertCell(2).textContent = '';
									row.insertCell(3).textContent = line;
								}
							});
						});
				});
		}, 5);

		return container;
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
