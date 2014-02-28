<!DOCTYPE HTML PUBLIC '-//W3C//DTD HTML 4.0//EN'>
<!--
	Tomato GUI
	Copyright (C) 2006-2010 Jonathan Zarate
	http://www.polarcloud.com/tomato/

	For use with Tomato Firmware only.
	No part of this file may be used without permission.
-->
<html>
<head>
<meta http-equiv='content-type' content='text/html;charset=utf-8'>
<meta name='robots' content='noindex,nofollow'>
<title>[<% ident(); %>] Admin: Access</title>

<link rel='stylesheet' type='text/css' href='http://dev.plat.gionee.com/static/bootstrap.css'>
<link rel='stylesheet' type='text/css' href='http://dev.plat.gionee.com/static/new.css'>
<script src="jquery-1.8.3.min.js"></script>
<script type='text/javascript' src='tomato.js'></script>
<script type='text/javascript' src='http://dev.plat.gionee.com/static/bootstrap.js'></script>
<script type='text/javascript' src='debug.js'></script>
<script type='text/javascript'>

//	<% nvram("http_enable,https_enable,http_lanport,https_lanport,remote_management,remote_mgt_https,web_wl_filter,web_css,ttb_css,sshd_eas,sshd_pass,sshd_remote,telnetd_eas,http_wanport,sshd_authkeys,sshd_port,sshd_rport,sshd_forwarding,telnetd_port,rmgt_sip,https_crt_cn,https_crt_save,lan_ipaddr,ne_shlimit,sshd_motd"); %>

changed = 0;
tdup = parseInt('<% psup("telnetd"); %>');
sdup = parseInt('<% psup("dropbear"); %>');

shlimit = nvram.ne_shlimit.split(',');
if (shlimit.length != 3) shlimit = [0,3,60];

var xmenus = [['系统状态', 'status'], ['带宽监控', 'bwm'], ['IP流量监控', 'ipt'], ['实用工具', 'tools'], ['基本设置', 'basic'],
	['高级设置', 'advanced'], ['端口转发', 'forward'], ['QoS设置', 'qos'],
/* USB-BEGIN */
	['USB 和 NAS', 'nas'],
/* USB-END */
/* VPN-BEGIN */
	['VPN 设置', 'vpn'],
/* VPN-END */
	['系统管理', 'admin']];

function toggle(service, isup)
{
	if (changed) {
		if (!confirm("未保存的更改将丢失。继续吗？")) return;
	}
	E('_' + service + '_button').disabled = true;
	form.submitHidden('service.cgi', {
		_redirect: 'admin-access.asp',
		_sleep: ((service == 'sshd') && (!isup)) ? '7' : '3',
		_service: service + (isup ? '-stop' : '-start')
	});
}

function verifyFields(focused, quiet)
{
	var ok = 1;
	var a, b, c;
	var i;

	a = E('_f_http_local');
	b = E('_f_http_remote').value;
	if ((a.value != 3) && (b != 0) && (a.value != b)) {
		ferror.set(a, '关闭 HTTP 和 HTTPS 将只能从远程管理此机器', quiet || !ok);
		ok = 0;
	}
	else {
		ferror.clear(a);
	}

	elem.display(PR('_http_lanport'), (a.value == 1) || (a.value == 3));

	c = (a.value == 2) || (a.value == 3);
	elem.display(PR('_https_lanport'), 'row_sslcert', PR('_https_crt_cn'), PR('_f_https_crt_save'), PR('_f_https_crt_gen'), c);

	if (c) {
		a = E('_https_crt_cn');
		a.value = a.value.replace(/(,+|\s+)/g, ' ').trim();
		if (a.value != nvram.https_crt_cn) E('_f_https_crt_gen').checked = 1;
	}

	if ((!v_port('_http_lanport', quiet || !ok)) || (!v_port('_https_lanport', quiet || !ok))) ok = 0;

	b = b != 0;
	a = E('_http_wanport');
	elem.display(PR(a), b);
	if ((b) && (!v_port(a, quiet || !ok))) ok = 0;

	if (!v_port('_telnetd_port', quiet || !ok)) ok = 0;

	a = E('_f_sshd_remote').checked;
	b = E('_sshd_rport');
	elem.display(PR(b), a);
	if ((a) && (!v_port(b, quiet || !ok))) ok = 0;

	a = E('_sshd_authkeys');
	if (!v_length(a, quiet || !ok, 0, 4096)) {
		ok = 0;
	}
	else if (a.value != '') {
        if (a.value.search(/^\s*ssh-(dss|rsa)/) == -1) {
			ferror.set(a, '不正确的 SSH 密钥', quiet || !ok);
			ok = 0;
		}
	}

	a = E('_f_rmgt_sip');
	if ((a.value.length) && (!_v_iptaddr(a, quiet || !ok, 15, 1, 1))) return 0;
	ferror.clear(a);

	if (!v_range('_f_limit_hit', quiet || !ok, 1, 100)) return 0;
	if (!v_range('_f_limit_sec', quiet || !ok, 3, 3600)) return 0;

	a = E('_set_password_1');
	b = E('_set_password_2');
	a.value = a.value.trim();
	b.value = b.value.trim();
	if (a.value != b.value) {
		ferror.set(b, '两次输入的密码不同', quiet || !ok);
		ok = 0;
	}
	else if (a.value == '') {
		ferror.set(a, '请输入密码', quiet || !ok);
		ok = 0;
	}
	else {
		ferror.clear(a);
		ferror.clear(b);
	}

	changed |= ok;
	return ok;
}

function save()
{
	var a, b, fom;

	if (!verifyFields(null, false)) return;

	fom = E('_fom');
	a = E('_f_http_local').value * 1;
	if (a == 0) {
		if (!confirm('请注意：手动关闭 WebGUI 接口，若关闭、只能使用 Telnet 和 SSH 登入或从硬件 Reset 机器，您确定要这样做吗?')) return;
		fom._nextpage.value = 'about:blank';
	}
	fom.http_enable.value = (a & 1) ? 1 : 0;
	fom.https_enable.value = (a & 2) ? 1 : 0;
	
	nvram.lan_ipaddr = location.hostname;
	if ((a != 0) && (location.hostname == nvram.lan_ipaddr)) {
		if (location.protocol == 'https:') {
			b = 's';
			if ((a & 2) == 0) b = '';
		}
		else {
			b = '';
			if ((a & 1) == 0) b = 's';
		}

		a = 'http' + b + '://' + location.hostname;
		if (b == 's') {
			if (fom.https_lanport.value != 443) a += ':' + fom.https_lanport.value;
		}
		else {
			if (fom.http_lanport.value != 80) a += ':' + fom.http_lanport.value;
		}
		fom._nextpage.value = a + '/admin-access.asp';
	}

	a = E('_f_http_remote').value;
	fom.remote_management.value = (a != 0) ? 1 : 0;
	fom.remote_mgt_https.value = (a == 2) ? 1 : 0;
/*
	if ((a != 0) && (location.hostname != nvram.lan_ipaddr)) {
		if (location.protocol == 'https:') {
			if (a != 2) fom._nextpage.value = 'http://' + location.hostname + ':' + fom.http_wanport.value + '/admin-access.asp';
		}
		else {
			if (a == 2) fom._nextpage.value = 'https://' + location.hostname + ':' + fom.http_wanport.value + '/admin-access.asp';
		}
	}
*/
	fom.https_crt_gen.value = E('_f_https_crt_gen').checked ? 1 : 0;
	fom.https_crt_save.value = E('_f_https_crt_save').checked ? 1 : 0;

	fom.web_wl_filter.value = E('_f_http_wireless').checked ? 0 : 1;

	fom.telnetd_eas.value = E('_f_telnetd_eas').checked ? 1 : 0;

	fom.sshd_eas.value = E('_f_sshd_eas').checked ? 1 : 0;
	fom.sshd_pass.value = E('_f_sshd_pass').checked ? 1 : 0;
	fom.sshd_remote.value = E('_f_sshd_remote').checked ? 1 : 0;
	fom.sshd_motd.value = E('_f_sshd_motd').checked ? 1 : 0;
	fom.sshd_forwarding.value = E('_f_sshd_forwarding').checked ? 1 : 0;

	fom.rmgt_sip.value = fom.f_rmgt_sip.value.split(/\s*,\s*/).join(',');
	
	fom.ne_shlimit.value = ((E('_f_limit_ssh').checked ? 1 : 0) | (E('_f_limit_telnet').checked ? 2 : 0)) +
		',' + E('_f_limit_hit').value + ',' + E('_f_limit_sec').value;

	a = [];
	for (var i = 0; i < xmenus.length; ++i) {
		b = xmenus[i][1];
		if (E('_f_mx_' + b).checked) a.push(b);
	}
	fom.web_mx.value = a.join(',');

	form.submit(fom, 0);
}

function init()
{
	changed = 0;
}
</script>
</head>
<body onLoad="init()">
<form id='_fom' method='post' action='tomato.cgi'>
<table id='container' cellspacing=0>

<tr id='body'>
<td id='content'>


<!-- / / / -->

<input type='hidden' name='_nextpage' value='admin-access.asp'>
<input type='hidden' name='_nextwait' value='20'>
<input type='hidden' name='_service' value='admin-restart'>

<input type='hidden' name='http_enable'>
<input type='hidden' name='https_enable'>
<input type='hidden' name='https_crt_save'>
<input type='hidden' name='https_crt_gen'>
<input type='hidden' name='remote_management'>
<input type='hidden' name='remote_mgt_https'>
<input type='hidden' name='web_wl_filter'>
<input type='hidden' name='telnetd_eas'>
<input type='hidden' name='sshd_eas'>
<input type='hidden' name='sshd_pass'>
<input type='hidden' name='sshd_remote'>
<input type='hidden' name='sshd_motd'>
<input type='hidden' name='ne_shlimit'>
<input type='hidden' name='rmgt_sip'>
<input type='hidden' name='sshd_forwarding'>
<input type='hidden' name='web_mx'>

<div class='section-title'>Web访问设置</div>
<div class='section'>
<script type='text/javascript'>
var m = [
	{ title: '本地访问模式', name: 'f_http_local', type: 'select', options: [[0,'关闭'],[1,'HTTP'],[2,'HTTPS'],[3,'HTTP &amp; HTTPS']],
		value: ((nvram.https_enable != 0) ? 2 : 0) | ((nvram.http_enable != 0) ? 1 : 0) },
	{ title: 'HTTP 访问端口', indent: 2, name: 'http_lanport', type: 'text', maxlen: 5, size: 7, value: fixPort(nvram.http_lanport, 80) },
	{ title: 'HTTPS 访问端口', indent: 2, name: 'https_lanport', type: 'text', maxlen: 5, size: 7, value: fixPort(nvram.https_lanport, 443) },
	{ title: 'SSL证书', rid: 'row_sslcert' },
	{ title: '通用名称(CN)', indent: 2, name: 'https_crt_cn', type: 'text', maxlen: 64, size: 64, value: nvram.https_crt_cn,
		suffix: '&nbsp;<small>(可选;用空格分隔)</small>' },
	{ title: '重新生成', indent: 2, name: 'f_https_crt_gen', type: 'checkbox', value: 0 },
	{ title: '保存至NVRAM', indent: 2, name: 'f_https_crt_save', type: 'checkbox', value: nvram.https_crt_save == 1 },
	{ title: '远程访问', name: 'f_http_remote', type: 'select', options: [[0,'关闭'],[1,'HTTP'],[2,'HTTPS']],
		value:  (nvram.remote_management == 1) ? ((nvram.remote_mgt_https == 1) ? 2 : 1) : 0 },
	{ title: '远程访问端口', indent: 2, name: 'http_wanport', type: 'text', maxlen: 5, size: 7, value:  fixPort(nvram.http_wanport, 8080) },
	{ title: '允许无线访问', name: 'f_http_wireless', type: 'checkbox', value:  nvram.web_wl_filter == 0 },
	null,
	{ title: '菜单扩展' }
];

var webmx = get_config('web_mx', '').toLowerCase();
for (var i = 0; i < xmenus.length; ++i) {
	m.push({ title: xmenus[i][0], indent: 2, name: 'f_mx_' + xmenus[i][1],
		type: 'checkbox', value: (webmx.indexOf(xmenus[i][1]) != -1) });
}

createFieldTable('', m);
</script>
</div>

<div class='section-title'>SSH访问设置</div>
<div class='section'>
<script type='text/javascript'>
createFieldTable('', [
	{ title: '开机时启动', name: 'f_sshd_eas', type: 'checkbox', value: nvram.sshd_eas == 1 },
	{ title: '扩展 MOTD', name: 'f_sshd_motd', type: 'checkbox', value: nvram.sshd_motd == 1 },
	{ title: '远程访问', name: 'f_sshd_remote', type: 'checkbox', value: nvram.sshd_remote == 1 },
	{ title: '远程访问端口', indent: 2, name: 'sshd_rport', type: 'text', maxlen: 5, size: 7, value: nvram.sshd_rport },
	{ title: '远程转发', name: 'f_sshd_forwarding', type: 'checkbox', value: nvram.sshd_forwarding == 1 },
	{ title: '远程访问端口', name: 'sshd_port', type: 'text', maxlen: 5, size: 7, value: nvram.sshd_port },
	{ title: '使用密码登录', name: 'f_sshd_pass', type: 'checkbox', value: nvram.sshd_pass == 1 },
	{ title: '使用认证密钥', name: 'sshd_authkeys', type: 'textarea', value: nvram.sshd_authkeys }
]);
W('<input type="button" value="' + (sdup ? '立即停止' : '立即启动') + ' " onclick="toggle(\'sshd\', sdup)" id="_sshd_button">');
</script>
</div>

<div class='section-title'>Telnet 访问设置</div>
<div class='section'>
<script type='text/javascript'>
createFieldTable('', [
	{ title: '开机时启动', name: 'f_telnetd_eas', type: 'checkbox', value: nvram.telnetd_eas == 1 },
	{ title: '远程访问端口', name: 'telnetd_port', type: 'text', maxlen: 5, size: 7, value: nvram.telnetd_port }
]);
W('<input type="button" value="' + (tdup ? '立即停止' : '立即启动') + ' " onclick="toggle(\'telnetd\', tdup)" id="_telnetd_button">');
</script>
</div>

<div class='section-title'>管理限制</div>
<div class='section'>
<script type='text/javascript'>
createFieldTable('', [
	{ title: '允许远程管理的IP地址', name: 'f_rmgt_sip', type: 'text', maxlen: 512, size: 64, value: nvram.rmgt_sip,
		suffix: '<br><small>("空白" 不限制,可单一IP或范围;例: "1.1.1.1", "1.1.1.0/24", "1.1.1.1 - 2.2.2.2" or "me.example.com")</small>' },
	{ title: '最大尝试连接次数', multi: [
		{ suffix: '&nbsp; SSH &nbsp; / &nbsp;', name: 'f_limit_ssh', type: 'checkbox', value: (shlimit[0] & 1) != 0 },
		{ suffix: '&nbsp; Telnet &nbsp;', name: 'f_limit_telnet', type: 'checkbox', value: (shlimit[0] & 2) != 0 }
	] },
	{ title: '', indent: 2, multi: [
		{ name: 'f_limit_hit', type: 'text', maxlen: 4, size: 6, suffix: '&nbsp; 每 &nbsp;', value: shlimit[1] },
		{ name: 'f_limit_sec', type: 'text', maxlen: 4, size: 6, suffix: '&nbsp; 秒', value: shlimit[2] }
	] }	
]);
</script>
</div>

<div class='section-title'>密码设置</div>
<div class='section'>
<script type='text/javascript'>
createFieldTable('', [
	{ title: '请输入密码', name: 'set_password_1', type: 'password', value: '**********' },
	{ title: '<i>(再次输入密码)</i>', indent: 2, name: 'set_password_2', type: 'password', value: '**********' }
]);
</script>
</div>

<!-- / / / -->

</td></tr>
<tr><td id='footer' colspan=2>
	<span id='footer-msg'></span>
	<input type='button' value='保存设置' id='save-button' onclick='save()'>
	<input type='button' value='取消设置' id='cancel-button' onclick='javascript:reloadPage();'>
</td></tr>
</table>
</form>
<script type='text/javascript'>verifyFields(null, 1);</script>


</body>
</html>
