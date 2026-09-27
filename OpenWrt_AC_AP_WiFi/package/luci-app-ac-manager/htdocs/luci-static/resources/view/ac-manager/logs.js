'use strict';
'require view';
'require ui';
'require rpc';
'require fs';

var callLogsRead = rpc.declare({
	object: 'ac-manager.logs',
	method: 'read',
	params: [ 'limit', 'offset' ],
	expect: { '': [] }
});

return view.extend({
	load: function() {
		return Promise.all([
			L.resolveDefault(callLogsRead(100, 0), [])
		]);
	},

	render: function(data) {
		var logs = data[0] || [];
		var container = E('div', { 'class': 'cbi-map' });

		container.appendChild(E('h2', {}, _('Audit Logs')));

		var controls = E('div', { 'class': 'cbi-section', 'style': 'margin-bottom:12px' });
		var limitInput = E('input', { 'type': 'number', 'class': 'cbi-input-text', 'value': '100', 'style': 'width:80px;margin-right:8px' });
		var offsetInput = E('input', { 'type': 'number', 'class': 'cbi-input-text', 'value': '0', 'style': 'width:80px;margin-right:8px' });
		var refreshBtn = E('button', { 'class': 'cbi-button cbi-button-reload' }, _('Refresh'));
		var exportBtn = E('button', { 'class': 'cbi-button cbi-button-save', 'style': 'margin-left:8px' }, _('Export CSV'));
		controls.appendChild(E('label', { 'style': 'margin-right:4px' }, _('Limit: '), limitInput));
		controls.appendChild(E('label', { 'style': 'margin-right:4px' }, _('Offset: '), offsetInput));
		controls.appendChild(refreshBtn);
		controls.appendChild(exportBtn);
		container.appendChild(controls);

		var table = E('table', { 'class': 'table', 'id': 'logs-table' });
		table.appendChild(E('tr', {},
			E('th', { 'style': 'width:15%' }, _('Timestamp')),
			E('th', { 'style': 'width:10%' }, _('User')),
			E('th', { 'style': 'width:10%' }, _('Site')),
			E('th', { 'style': 'width:12%' }, _('Action')),
			E('th', { 'style': 'width:15%' }, _('Resource')),
			E('th', { 'style': 'width:8%' }, _('Result')),
			E('th', {}, _('Detail'))
		));
		container.appendChild(table);

		function formatTime(t) {
			if (!t || t === 0) return '-';
			var d = new Date(t * 1000);
			return d.toLocaleString();
		}

		function refreshLogs() {
			var limit = parseInt(limitInput.value) || 100;
			var offset = parseInt(offsetInput.value) || 0;
			return L.resolveDefault(callLogsRead(limit, offset), []).then(function(entries) {
				while (table.rows.length > 1) table.deleteRow(1);
				(entries || []).forEach(function(log) {
					var row = table.insertRow(-1);
					row.insertCell(0).textContent = formatTime(log.timestamp);
					row.insertCell(1).textContent = log.user_id || '-';
					row.insertCell(2).textContent = log.site_id || '-';
					row.insertCell(3).textContent = log.action || '-';
					row.insertCell(4).textContent = log.resource || '-';
					var cell = row.insertCell(5);
					cell.textContent = log.result || '-';
					cell.style.color = (log.result === 'success') ? '#4caf50' : '#f44336';
					row.insertCell(6).textContent = log.detail || '-';
				});
				if (table.rows.length <= 1) {
					var row = table.insertRow(-1);
					var cell = row.insertCell(0);
					cell.colSpan = 7;
					cell.textContent = _('No audit logs found');
					cell.style.textAlign = 'center';
				}
			});
		}

		function exportCSV() {
			var csv = 'timestamp,user_id,site_id,action,resource,result,detail\n';
			for (var i = 1; i < table.rows.length; i++) {
				var row = table.rows[i];
				var cells = [];
				for (var j = 0; j < row.cells.length; j++) {
					cells.push('"' + (row.cells[j].textContent || '').replace(/"/g, '""') + '"');
				}
				csv += cells.join(',') + '\n';
			}
			var blob = new Blob([csv], { type: 'text/csv' });
			var link = document.createElement('a');
			link.href = URL.createObjectURL(blob);
			link.download = 'ac-manager-audit-logs.csv';
			link.click();
			URL.revokeObjectURL(link.href);
		}

		refreshBtn.addEventListener('click', refreshLogs);
		exportBtn.addEventListener('click', exportCSV);

		(function() {
			while (table.rows.length > 1) table.deleteRow(1);
			(logs || []).forEach(function(log) {
				var row = table.insertRow(-1);
				row.insertCell(0).textContent = formatTime(log.timestamp);
				row.insertCell(1).textContent = log.user_id || '-';
				row.insertCell(2).textContent = log.site_id || '-';
				row.insertCell(3).textContent = log.action || '-';
				row.insertCell(4).textContent = log.resource || '-';
				var cell = row.insertCell(5);
				cell.textContent = log.result || '-';
				cell.style.color = (log.result === 'success') ? '#4caf50' : '#f44336';
				row.insertCell(6).textContent = log.detail || '-';
			});
			if (table.rows.length <= 1) {
				var row = table.insertRow(-1);
				var cell = row.insertCell(0);
				cell.colSpan = 7;
				cell.textContent = _('No audit logs found');
				cell.style.textAlign = 'center';
			}
		})();

		return container;
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
