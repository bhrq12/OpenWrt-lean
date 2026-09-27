'use strict';
'require view';
'require form';
'require uci';
'require ui';
'require fs';

return view.extend({
	load: function() {
		return Promise.all([
			uci.load('ac-manager')
		]);
	},

	render: function() {
		var m, s, o;

		m = new form.Map('ac-manager', _('AC Manager Configuration'),
			_('Configuration for the AC Controller service. Changes are saved to UCI and synced to INI format for the daemon.'));

		s = m.section(form.TypedSection, 'server', _('Server Settings'));
		s.addremove = false;
		s.anonymous = true;

		o = s.option(form.Value, 'host', _('Listen Host'), _('Bind address for WebSocket server'));
		o.default = '0.0.0.0';
		o.datatype = 'ipaddr';

		o = s.option(form.Value, 'port', _('Listen Port'), _('WebSocket server port'));
		o.default = '8080';
		o.datatype = 'port';

		o = s.option(form.Value, 'cert_file', _('Certificate File'), _('TLS certificate path'));
		o.default = '/etc/ac-manager/cert.pem';

		o = s.option(form.Value, 'key_file', _('Private Key File'), _('TLS private key path'));
		o.default = '/etc/ac-manager/key.pem';

		o = s.option(form.Value, 'ca_file', _('CA Certificate'), _('CA certificate for client verification'));
		o.default = '/etc/ac-manager/ca.pem';

		s = m.section(form.TypedSection, 'database', _('Database Settings'));
		s.addremove = false;
		s.anonymous = true;

		o = s.option(form.ListValue, 'type', _('Database Type'), _('SQLite is recommended for standalone mode'));
		o.value('sqlite', _('SQLite'));
		o.value('mysql', _('MySQL (requires conditional compilation)'));
		o.default = 'sqlite';

		o = s.option(form.Value, 'path', _('Database Path'), _('SQLite database file path'));
		o.default = '/var/lib/ac-manager/ac-manager.db';
		o.depends('type', 'sqlite');

		s = m.section(form.TypedSection, 'redis', _('Redis Settings'));
		s.addremove = false;
		s.anonymous = true;

		o = s.option(form.Flag, 'enabled', _('Enable Redis'), _('Enable for cluster or multi-site mode'));
		o.default = '0';

		o = s.option(form.Value, 'host', _('Redis Host'));
		o.default = '127.0.0.1';
		o.depends('enabled', '1');
		o.datatype = 'host';

		o = s.option(form.Value, 'port', _('Redis Port'));
		o.default = '6379';
		o.depends('enabled', '1');
		o.datatype = 'port';

		o = s.option(form.Value, 'password', _('Redis Password'), _('Leave empty for no auth'));
		o.depends('enabled', '1');
		o.password = true;

		s = m.section(form.TypedSection, 'heartbeat', _('Heartbeat Settings'));
		s.addremove = false;
		s.anonymous = true;

		o = s.option(form.Value, 'interval', _('Heartbeat Interval (seconds)'));
		o.default = '30';
		o.datatype = 'range(10,300)';

		o = s.option(form.Value, 'timeout', _('Heartbeat Timeout (seconds)'));
		o.default = '120';
		o.datatype = 'range(60,600)';

		s = m.section(form.TypedSection, 'logging', _('Logging Settings'));
		s.addremove = false;
		s.anonymous = true;

		o = s.option(form.ListValue, 'level', _('Log Level'));
		o.value('debug', _('Debug'));
		o.value('info', _('Info'));
		o.value('warn', _('Warning'));
		o.value('error', _('Error'));
		o.value('fatal', _('Fatal'));
		o.default = 'info';

		o = s.option(form.Value, 'file', _('Log File Path'));
		o.default = '/var/log/ac-manager.log';

		o = s.option(form.Value, 'max_size', _('Max Log Size (bytes)'));
		o.default = '10485760';
		o.datatype = 'uinteger';

		o = s.option(form.Value, 'rotate_count', _('Log Rotation Count'));
		o.default = '5';
		o.datatype = 'uinteger';

		return m.render();
	},

	handleSaveApply: function(ev, mode) {
		return this.handleSave(ev).then(function() {
			return fs.exec_direct('/usr/sbin/ac-manager-uci-sync', [])
				.then(function() {
					return fs.exec_direct('/etc/init.d/ac-manager', ['restart']);
				})
				.then(function() {
					ui.addNotification(null, E('p', _('Configuration saved and AC Manager restarted.')), 'info');
				})
				.catch(function(err) {
					ui.addNotification(null, E('p', _('Sync/Restart failed: ') + (err.message || err)), 'error');
				});
		});
	}
});
