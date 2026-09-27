'use strict';
'require view';
'require ui';
'require rpc';
'require poll';
'require dom';

var callDevicesList = rpc.declare({
	object: 'ac-manager.devices',
	method: 'list',
	params: [],
	expect: { '': [] }
});

var callDeviceGet = rpc.declare({
	object: 'ac-manager.devices',
	method: 'get',
	params: [ 'dev_sn' ],
	expect: { '': {} }
});

function statusText(status) {
	switch (status) {
		case 0: return _('Offline');
		case 1: return _('Online');
		case 2: return _('Upgrading');
		case 3: return _('Maintenance');
		case 4: return _('Disconnected');
		default: return _('Unknown');
	}
}

function statusColor(status) {
	switch (status) {
		case 1: return '#4caf50';
		case 0: case 4: return '#f44336';
		case 2: case 3: return '#ff9800';
		default: return '#9e9e9e';
	}
}

function formatTime(t) {
	if (!t || t === 0) return '-';
	var d = new Date(t * 1000);
	return d.toLocaleString();
}

return view.extend({
	load: function() {
		return Promise.all([
			L.resolveDefault(callDevicesList(), [])
		]);
	},

	render: function(data) {
		var devices = data[0] || [];
		var container = E('div', { 'class': 'cbi-map' });

		container.appendChild(E('h2', {}, _('AP Devices')));

		var filterDiv = E('div', { 'class': 'cbi-section', 'style': 'margin-bottom:12px' });
		var siteFilter = E('select', { 'class': 'cbi-input-select', 'style': 'margin-right:8px' });
		siteFilter.appendChild(E('option', { 'value': '' }, _('All Sites')));
		var statusFilter = E('select', { 'class': 'cbi-input-select' });
		statusFilter.appendChild(E('option', { 'value': '' }, _('All Status')));
		statusFilter.appendChild(E('option', { 'value': '1' }, _('Online')));
		statusFilter.appendChild(E('option', { 'value': '0' }, _('Offline')));
		statusFilter.appendChild(E('option', { 'value': '3' }, _('Maintenance')));
		filterDiv.appendChild(E('label', { 'style': 'margin-right:8px' }, _('Filter: '), siteFilter));
		filterDiv.appendChild(statusFilter);
		container.appendChild(filterDiv);

		var table = E('table', { 'class': 'table', 'id': 'devices-table' });
		table.appendChild(E('tr', {},
			E('th', {}, _('Device SN')),
			E('th', {}, _('Model')),
			E('th', {}, _('Firmware')),
			E('th', {}, _('Site')),
			E('th', {}, _('Status')),
			E('th', {}, _('Last Heartbeat')),
			E('th', {}, _('Config Version')),
			E('th', {}, _('CPU %')),
			E('th', {}, _('MEM %')),
			E('th', {}, _('Clients'))
		));

		container.appendChild(table);

		function refreshTable() {
			return L.resolveDefault(callDevicesList(), []).then(function(devs) {
				while (table.rows.length > 1) table.deleteRow(1);
				(devs || []).forEach(function(dev) {
					var siteVal = siteFilter.value;
					var statusVal = statusFilter.value;
					if (siteVal && dev.site_id !== siteVal) return;
					if (statusVal !== '' && String(dev.status) !== statusVal) return;
					var row = table.insertRow(-1);
					row.style.cursor = 'pointer';
					row.addEventListener('click', function() {
						showDeviceDetail(dev.dev_sn);
					});
					row.insertCell(0).textContent = dev.dev_sn || '-';
					row.insertCell(1).textContent = dev.dev_model || '-';
					row.insertCell(2).textContent = dev.fw_version || '-';
					row.insertCell(3).textContent = dev.site_id || '-';
					var cell = row.insertCell(4);
					cell.textContent = statusText(dev.status);
					cell.style.color = statusColor(dev.status);
					cell.style.fontWeight = 'bold';
					row.insertCell(5).textContent = formatTime(dev.last_heartbeat);
					row.insertCell(6).textContent = dev.config_version || '-';
					row.insertCell(7).textContent = dev.cpu_usage != null ? dev.cpu_usage : '-';
					row.insertCell(8).textContent = dev.mem_usage != null ? dev.mem_usage : '-';
					row.insertCell(9).textContent = dev.client_count != null ? dev.client_count : '-';
				});
				if (table.rows.length <= 1) {
					var row = table.insertRow(-1);
					var cell = row.insertCell(0);
					cell.colSpan = 10;
					cell.textContent = _('No devices found');
					cell.style.textAlign = 'center';
				}
			});
		}

		function showDeviceDetail(dev_sn) {
			L.resolveDefault(callDeviceGet(dev_sn), {}).then(function(dev) {
				var modal = E('div', { 'class': 'modal' });
				var content = E('div', { 'class': 'modal-body', 'style': 'background:#fff;padding:20px;margin:10% auto;width:60%;border-radius:8px' });
				content.appendChild(E('h3', {}, _('Device Detail: ') + dev_sn));
				var info = E('table', { 'class': 'table' });
				info.appendChild(E('tr', {}, E('td', { 'style': 'width:40%' }, E('strong', {}, _('Device SN:'))), E('td', {}, dev.dev_sn || '-')));
				info.appendChild(E('tr', {}, E('td', {}, E('strong', {}, _('Model:'))), E('td', {}, dev.dev_model || '-')));
				info.appendChild(E('tr', {}, E('td', {}, E('strong', {}, _('Firmware:'))), E('td', {}, dev.fw_version || '-')));
				info.appendChild(E('tr', {}, E('td', {}, E('strong', {}, _('Site ID:'))), E('td', {}, dev.site_id || '-')));
				info.appendChild(E('tr', {}, E('td', {}, E('strong', {}, _('Group ID:'))), E('td', {}, dev.group_id || '-')));
				info.appendChild(E('tr', {}, E('td', {}, E('strong', {}, _('Status:'))), E('td', { 'style': 'color:' + statusColor(dev.status) }, statusText(dev.status))));
				info.appendChild(E('tr', {}, E('td', {}, E('strong', {}, _('Current Config:'))), E('td', {}, dev.cur_config_id || '-')));
				info.appendChild(E('tr', {}, E('td', {}, E('strong', {}, _('Config Version:'))), E('td', {}, String(dev.config_version || '-'))));
				info.appendChild(E('tr', {}, E('td', {}, E('strong', {}, _('Register Time:'))), E('td', {}, formatTime(dev.register_time))));
				info.appendChild(E('tr', {}, E('td', {}, E('strong', {}, _('Last Heartbeat:'))), E('td', {}, formatTime(dev.last_heartbeat))));
				content.appendChild(info);
				var closeBtn = E('button', { 'class': 'cbi-button', 'style': 'margin-top:12px', 'click': function() { document.body.removeChild(modal); } }, _('Close'));
				content.appendChild(closeBtn);
				modal.appendChild(content);
				modal.style.position = 'fixed';
				modal.style.top = '0';
				modal.style.left = '0';
				modal.style.width = '100%';
				modal.style.height = '100%';
				modal.style.background = 'rgba(0,0,0,0.5)';
				modal.style.zIndex = '9999';
				document.body.appendChild(modal);
			});
		}

		siteFilter.addEventListener('change', refreshTable);
		statusFilter.addEventListener('change', refreshTable);

		refreshTable();

		poll.add(refreshTable, 30);

		return container;
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
