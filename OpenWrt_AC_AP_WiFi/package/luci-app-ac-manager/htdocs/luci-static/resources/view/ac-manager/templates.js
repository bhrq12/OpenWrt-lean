'use strict';
'require view';
'require ui';
'require rpc';

var callTemplatesList = rpc.declare({
	object: 'ac-manager.templates',
	method: 'list',
	params: [],
	expect: { '': [] }
});

var callTemplateGet = rpc.declare({
	object: 'ac-manager.templates',
	method: 'get',
	params: [ 'config_id' ],
	expect: { '': {} }
});

function typeText(type) {
	switch (type) {
		case 1: return _('SSID');
		case 2: return _('Radio');
		case 3: return _('Global');
		default: return _('Unknown');
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
			L.resolveDefault(callTemplatesList(), [])
		]);
	},

	render: function(data) {
		var templates = data[0] || [];
		var container = E('div', { 'class': 'cbi-map' });

		container.appendChild(E('h2', {}, _('Configuration Templates')));

		var typeFilter = E('select', { 'class': 'cbi-input-select', 'style': 'margin-bottom:12px' });
		typeFilter.appendChild(E('option', { 'value': '' }, _('All Types')));
		typeFilter.appendChild(E('option', { 'value': '1' }, _('SSID')));
		typeFilter.appendChild(E('option', { 'value': '2' }, _('Radio')));
		typeFilter.appendChild(E('option', { 'value': '3' }, _('Global')));

		container.appendChild(E('div', { 'class': 'cbi-section' },
			E('label', {}, _('Filter: '), typeFilter)));

		var table = E('table', { 'class': 'table', 'id': 'templates-table' });
		table.appendChild(E('tr', {},
			E('th', {}, _('Config ID')),
			E('th', {}, _('Template Name')),
			E('th', {}, _('Type')),
			E('th', {}, _('Version')),
			E('th', {}, _('Creator')),
			E('th', {}, _('Created')),
			E('th', {}, _('Signature'))
		));
		container.appendChild(table);

		function refreshTable() {
			while (table.rows.length > 1) table.deleteRow(1);
			templates.forEach(function(tpl) {
				if (typeFilter.value && String(tpl.template_type) !== typeFilter.value) return;
				var row = table.insertRow(-1);
				row.style.cursor = 'pointer';
				row.addEventListener('click', function() {
					showTemplateDetail(tpl.config_id);
				});
				row.insertCell(0).textContent = tpl.config_id || '-';
				row.insertCell(1).textContent = tpl.template_name || '-';
				row.insertCell(2).textContent = typeText(tpl.template_type);
				row.insertCell(3).textContent = String(tpl.version || '-');
				row.insertCell(4).textContent = tpl.creator || '-';
				row.insertCell(5).textContent = formatTime(tpl.create_time);
				var sigCell = row.insertCell(6);
				sigCell.textContent = tpl.signature ? _('✓ Signed') : _('✗ Unsigned');
				sigCell.style.color = tpl.signature ? '#4caf50' : '#f44336';
			});
			if (table.rows.length <= 1) {
				var row = table.insertRow(-1);
				var cell = row.insertCell(0);
				cell.colSpan = 7;
				cell.textContent = _('No templates found');
				cell.style.textAlign = 'center';
			}
		}

		function showTemplateDetail(config_id) {
			L.resolveDefault(callTemplateGet(config_id), {}).then(function(tpl) {
				var modal = E('div', { 'style': 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.5);z-index:9999' });
				var content = E('div', { 'style': 'background:#fff;padding:20px;margin:5% auto;width:70%;border-radius:8px;max-height:80%;overflow-y:auto' });
				content.appendChild(E('h3', {}, _('Template Detail: ') + config_id));
				var info = E('table', { 'class': 'table' });
				info.appendChild(E('tr', {}, E('td', { 'style': 'width:30%' }, E('strong', {}, _('Config ID:'))), E('td', {}, tpl.config_id || '-')));
				info.appendChild(E('tr', {}, E('td', {}, E('strong', {}, _('Template Name:'))), E('td', {}, tpl.template_name || '-')));
				info.appendChild(E('tr', {}, E('td', {}, E('strong', {}, _('Type:'))), E('td', {}, typeText(tpl.template_type))));
				info.appendChild(E('tr', {}, E('td', {}, E('strong', {}, _('Version:'))), E('td', {}, String(tpl.version || '-'))));
				info.appendChild(E('tr', {}, E('td', {}, E('strong', {}, _('Creator:'))), E('td', {}, tpl.creator || '-')));
				info.appendChild(E('tr', {}, E('td', {}, E('strong', {}, _('Created:'))), E('td', {}, formatTime(tpl.create_time))));
				info.appendChild(E('tr', {}, E('td', {}, E('strong', {}, _('Signature:'))), E('td', { 'style': 'word-break:break-all;font-family:monospace;font-size:0.8em' }, tpl.signature || '-')));
				content.appendChild(info);
				content.appendChild(E('h4', {}, _('Config Content:')));
				var pre = E('pre', { 'style': 'background:#f5f5f5;padding:12px;border-radius:4px;max-height:300px;overflow-y:auto;font-size:0.85em' }, tpl.config_content || '-');
				content.appendChild(pre);
				var closeBtn = E('button', { 'class': 'cbi-button', 'style': 'margin-top:12px', 'click': function() { document.body.removeChild(modal); } }, _('Close'));
				content.appendChild(closeBtn);
				modal.appendChild(content);
				document.body.appendChild(modal);
			});
		}

		typeFilter.addEventListener('change', refreshTable);
		refreshTable();

		return container;
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
