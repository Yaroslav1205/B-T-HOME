"""Run relay UI smoke checks in a local Chromium/Edge browser with an in-page fake API."""
import argparse
from pathlib import Path
import subprocess
import sys

MOCK = r"""
window.mockState={mode:1,ready:true,on:false,saved_on:false,output_known:true,storage_known:true,error:'ok',mac:'02:11:22:33:1B:A4'};
window.mockFail=false;window.mockStorageFail=false;window.mockSaveFail=false;
window.writeCalls=0;window.settingsWrites=0;window.mockAuthorized=false;
window.mockSettings={ssid:'B-T HOME 1BA4',username:'admin',wifi_pending:false};
window.mockPassword='admin';window.mockWifiPassword='12345678';
window.mockVoltages={mains:{volts:230.5,status:'ok',age_ms:0},battery:{volts:12.4,status:'ok',age_ms:0},current_period:{present:true,duration_ms:120000},previous_period:{present:false,duration_ms:7200000}};
window.mockVoltageFail=false;
window.mockZigbee={state:'idle',channel:0,seconds_left:0};window.zigbeeWrites=0;window.mockZigbeeFail=false;
window.mockConfirm=true;window.confirm=()=>mockConfirm;
window.mockBuzzer={available:true,interval_seconds:20};window.buzzerWrites=0;window.mockBuzzerFail=false;
window.mockOutages={available:true,group:2};window.outageWrites=0;window.mockOutageFail=false;
window.mockBattery={available:true,discharge_ma:58};window.batteryWrites=0;window.mockBatteryFail=false;
window.mockOta={supported:true,connected:true,busy:false,current_version:'0.2.0',version:'0.3.0',state:'idle',received:0,total:1000};
window.otaWrites=0;
window.fetch=async function(url,options){
 if(mockFail)throw new Error('offline');
 const reply=(status,data)=>({status,ok:status>=200&&status<300,json:async()=>({...data})});
 if(url==='/api/login'){
  const data=JSON.parse(options.body);
  if(data.username!==mockSettings.username||data.password!==mockPassword)return reply(401,{error:'invalid_credentials'});
  mockAuthorized=true;return reply(200,{ok:true});
 }
 if(url==='/api/logout'){mockAuthorized=false;return reply(200,{ok:true});}
 if(!mockAuthorized)return reply(401,{error:'login_required'});
 if(url==='/api/ota'){
  if(options.method==='POST'){
   if(options.headers['X-BTH-Request']!=='1')return reply(403,{error:'same_origin_required'});
   if(mockOta.busy)return reply(409,{error:'ota_busy'});
   otaWrites++;mockOta.state=JSON.parse(options.body).action==='check'?'available':'downloading';
   mockOta.busy=mockOta.state==='downloading';
  }
  return reply(200,mockOta);
 }
 if(url==='/api/relay-mode'){
  const mode=JSON.parse(options.body).mode;
  if(!Number.isInteger(mode)||mode<0||mode>2)return reply(400,{error:'invalid_mode'});
  mockState.mode=mode;mockState.on=mode===1?mockState.saved_on:false;
  return reply(200,mockState);
 }
 if(url==='/api/buzzer'){
  if(options.method==='PUT'){
   if(mockBuzzerFail)return reply(503,{error:'save_failed'});
   const seconds=JSON.parse(options.body).interval_seconds;
   if(!Number.isInteger(seconds)||seconds<0||seconds>86400)return reply(400,{error:'invalid_interval'});
   buzzerWrites++;mockBuzzer.interval_seconds=seconds;
  }
  return reply(200,mockBuzzer);
 }
 if(url==='/api/outages'){
  if(options.method==='PUT'){
   if(mockOutageFail)return reply(503,{error:'save_failed'});
   const group=JSON.parse(options.body).group;
   if(!Number.isInteger(group)||group<0||group>12)return reply(400,{error:'invalid_group'});
   outageWrites++;mockOutages.group=group;
  }
  return reply(200,mockOutages);
 }
 if(url==='/api/battery'){
  if(options.method==='PUT'){
   if(mockBatteryFail)return reply(503,{error:'save_failed'});
   const seconds=JSON.parse(options.body).discharge_ma;
   if(!Number.isInteger(seconds)||seconds<1||seconds>100000)return reply(400,{error:'invalid_interval'});
   batteryWrites++;mockBattery.discharge_ma=seconds;
  }
  return reply(200,mockBattery);
 }
 if(url==='/api/zigbee'){
  if(mockZigbeeFail)return reply(503,{error:'zigbee_busy'});
  if(options.method==='POST'){
   const action=JSON.parse(options.body).action;zigbeeWrites++;
   mockZigbee={state:action==='join'?'joining':'idle',channel:0,seconds_left:action==='join'?120:0};
   return reply(202,{accepted:true});
  }
  return reply(200,mockZigbee);
 }
 if(url==='/api/voltages')return reply(mockVoltageFail?503:200,mockVoltages);
 if(url==='/api/settings'){
  if(options.method==='PUT'){
   settingsWrites++;const submitted=JSON.parse(options.body);
   const data={ssid:mockSettings.ssid,username:mockSettings.username,wifi_password:'',web_password:'',...submitted};
   if(mockSaveFail)return reply(503,{error:'save_failed'});
   const login_required=data.username!==mockSettings.username||!!data.web_password;
   const reconnect=data.ssid!==mockSettings.ssid||(!!data.wifi_password&&data.wifi_password!==mockWifiPassword);
   for(const key of ['primary','backup']){
    if(key+'_ssid' in data){mockSettings[key+'_ssid']=data[key+'_ssid'];mockSettings[key+'_open']=data[key+'_open'];}
   }
   window.lastSettingsWrite=submitted;
   mockSettings.ssid=data.ssid;mockSettings.username=data.username;
   if(data.web_password)mockPassword=data.web_password;
   if(data.wifi_password)mockWifiPassword=data.wifi_password;
   if(login_required)mockAuthorized=false;
   return reply(200,{ok:true,login_required,reconnect,restart_required:false,ssid:data.ssid});
  }
  return reply(200,mockSettings);
 }
 if(options.method==='PUT'){
  writeCalls++;await new Promise(resolve=>setTimeout(resolve,80));
  if(mockStorageFail){mockState.ready=false;mockState.storage_known=false;mockState.error='hardware_error';}
  else{mockState.on=JSON.parse(options.body);mockState.saved_on=mockState.on;}
 }
 return reply(mockState.ready?200:503,mockState);
};
"""
CHECKS = r"""
(async()=>{
 const wait=ms=>new Promise(r=>setTimeout(r,ms));
 const check=(ok,name)=>{if(!ok)throw new Error(name);};
 const submit=id=>element(id).dispatchEvent(new Event('submit',{bubbles:true,cancelable:true}));
 const signIn=async(user,password)=>{element('loginUser').value=user;element('loginPassword').value=password;submit('loginForm');await wait(100);};
 try{
  await wait(100);
  check(!loginCard.hidden&&app.hidden&&button.disabled,'login gate');
  button.click();check(writeCalls===0,'no unauthenticated UI command');
  await signIn('admin','wrong');check(!signedIn&&!loginCard.hidden,'wrong password');
  await signIn('admin','admin');check(signedIn&&!app.hidden&&!current.on,'default login');
  check(element('batteryCurrent').value==='58'&&!element('batteryFields').disabled,'battery current loaded');
  element('batteryCurrent').value='1000';submit('batteryForm');await wait(50);
  check(mockBattery.discharge_ma===1000&&batteryWrites===1,'battery current saved');
  await loadSettings();check(element('batteryCurrent').value==='1000','battery current restored');
  for(const bad of ['0','-1','1.5','100001','']){
    element('batteryCurrent').value=bad;submit('batteryForm');await wait(50);
    check(batteryWrites===1&&element('batteryMessage').classList.contains('error'),'invalid battery current rejected');
  }
  mockBatteryFail=true;element('batteryCurrent').value='500';submit('batteryForm');await wait(50);
  check(mockBattery.discharge_ma===1000&&element('batteryMessage').classList.contains('error'),'battery save failure');
  mockBatteryFail=false;await loadSettings();check(element('batteryCurrent').value==='1000','battery failed value not persisted');
  check(element('buzzerInterval').value==='20'&&!element('buzzerFields').disabled,'buzzer loaded');
  element('buzzerInterval').value='0';submit('buzzerForm');await wait(50);
  check(mockBuzzer.interval_seconds===0&&buzzerWrites===1&&element('buzzerMessage').textContent.includes('вимкнено'),'buzzer disabled');
  await loadSettings();check(element('buzzerInterval').value==='0','disabled setting restored');
  element('buzzerInterval').value='60';submit('buzzerForm');await wait(50);
  check(mockBuzzer.interval_seconds===60&&buzzerWrites===2,'buzzer custom interval');
  element('buzzerInterval').value='1.5';submit('buzzerForm');await wait(50);
  check(buzzerWrites===2&&element('buzzerMessage').classList.contains('error'),'fraction rejected');
  element('buzzerInterval').value='-1';submit('buzzerForm');await wait(50);
  check(buzzerWrites===2,'negative rejected');
  element('buzzerInterval').value='86401';submit('buzzerForm');await wait(50);
  check(buzzerWrites===2,'excess interval rejected');
  element('buzzerInterval').value='';submit('buzzerForm');await wait(50);
  check(buzzerWrites===2,'empty interval rejected');
  mockBuzzerFail=true;element('buzzerInterval').value='30';submit('buzzerForm');await wait(50);
  check(mockBuzzer.interval_seconds===60&&element('buzzerMessage').classList.contains('error'),'buzzer save failure');
  mockBuzzerFail=false;await loadSettings();check(element('buzzerInterval').value==='60','failed value not persisted');
  mockBuzzer.available=false;await loadSettings();check(element('buzzerPanel').parentElement.hidden,'absent buzzer hidden');
  mockBuzzer.available=true;await loadSettings();check(!element('buzzerPanel').parentElement.hidden,'buzzer restored');
  check(element('deviceMac').textContent===mockState.mac,'device MAC from firmware');
  mockState.wifi_mode='matter';mockSettings.wifi_managed_by_matter=true;
  await refresh();await loadSettings();
  check(!element('matterInfo').hidden&&element('wifiSettings').hidden&&!element('matterWifiNotice').hidden,'Matter provisioning UI');
  mockState.wifi_mode='ap';mockSettings.wifi_managed_by_matter=false;
  await refresh();await loadSettings();
  check(element('matterInfo').hidden&&!element('wifiSettings').hidden,'AP provisioning UI');
  mockState.mac='02:aa:bb:cc:dd:ee';await refresh();
  check(element('deviceMac').textContent==='02:AA:BB:CC:DD:EE','MAC updated and normalized');
  mockState.mac='invalid';await refresh();check(element('deviceMac').textContent==='\u2014','invalid MAC unavailable');
  mockState.mac='02:11:22:33:1B:A4';await refresh();
  check(element('ssid').value==='B-T HOME 1BA4'&&!element('wifiPassword').value,'settings without passwords');
  check(element('mainsVoltage').textContent==='230.5 V'&&element('batteryVoltage').textContent==='12.4 V','live voltages');
  check(element('app').lastElementChild.id==='otaCard','OTA is the bottom panel');
  mockVoltages.display={age_ms:0,average_volts:231.2,battery_minutes:725,mains_present:false,relay_on:true,ipv4:'192.168.1.12',outage_state:'interval',outage_group:2,outage_begin:480,outage_end:600};
  await refresh();
  check(element('averageVoltage').textContent==='231.2 V'&&element('batteryRemaining').textContent==='12:05','OLED average and autonomy');
  check(element('readingIp').textContent==='192.168.1.12'&&element('readingRelay').textContent==='Увімкнено','OLED IP and relay');
  check(element('readingGroup').textContent==='група 2'&&element('readingOutage').textContent==='08:00–10:00','OLED schedule');
  mockVoltages.display.battery_minutes=6000;await refresh();
  check(element('batteryRemaining').textContent==='>99:59','OLED autonomy overflow');
  mockVoltages.display.battery_minutes=null;mockVoltages.display.mains_present=true;
  mockVoltages.display.outage_state='stale';await refresh();
  check(element('batteryRemaining').textContent==='—'&&element('remainingStatus').textContent==='Живлення від мережі','OLED mains autonomy');
  check(element('readingOutage').textContent==='Дані застарілі','OLED stale schedule');
  mockVoltages.display.age_ms=2499;await refresh();await wait(300);
  check(element('averageVoltage').textContent==='—'&&element('readingOutage').textContent==='—','OLED snapshot expires');
  delete mockVoltages.display;await refresh();
  check(!element('zigbeeJoin').disabled&&!element('zigbeePanel').hidden,'zigbee available');
  element('zigbeeJoin').click();await wait(100);
  check(zigbeeWrites===1&&element('zigbeeJoin').disabled&&element('zigbeeState').textContent.includes('120'),'pairing window');
  mockZigbee={state:'joined',channel:15,seconds_left:0};await refresh();
  check(element('zigbeeState').textContent.includes('15')&&element('zigbeeJoin').disabled,'joined status');
  mockConfirm=false;element('zigbeeReset').click();await wait(100);check(zigbeeWrites===1,'cancel reset');
  mockConfirm=true;element('zigbeeReset').click();await wait(100);check(zigbeeWrites===2&&!element('zigbeeJoin').disabled,'explicit reset');
  mockZigbeeFail=true;element('zigbeeJoin').click();await wait(100);
  check(element('zigbeeMessage').classList.contains('error')&&zigbeeWrites===2,'pairing failure');
  mockZigbeeFail=false;mockZigbee.state='disabled';await refresh();
  check(element('zigbeePanel').parentElement.hidden,'disabled build hides zigbee');
  mockZigbee.state='idle';await refresh();
  check(element('currentDuration').textContent==='2 хв'&&element('currentPower').textContent==='Живлення є','current mains interval');
  check(element('previousDuration').textContent==='2.0 год'&&element('previousPower').textContent==='Живлення не було','previous outage interval');
  mockVoltages.current_period={present:false,duration_ms:3599999};mockVoltages.previous_period=null;await refresh();
  check(element('currentDuration').textContent==='59 хв'&&element('currentPower').textContent==='Живлення немає'&&element('previousDuration').textContent==='—','outage and unknown previous');
  mockVoltages.current_period.duration_ms=3600000;await refresh();
  check(element('currentDuration').textContent==='1.0 год','hour boundary');
  mockVoltages.mains.age_ms=2499;await refresh();await wait(300);
  check(element('currentDuration').textContent==='—','stale history cleared');
  mockVoltages.mains={volts:null,status:'uncalibrated',age_ms:0};
  mockVoltages.battery={volts:29.8,status:'ok',age_ms:0};await refresh();
  check(element('mainsVoltage').textContent==='—'&&element('mainsStatus').textContent.includes('калібрування'),'uncalibrated mains');
  check(element('currentDuration').textContent==='—','uncalibrated history cleared');
  check(element('batteryVoltage').textContent==='29.8 V','independent battery');
  mockVoltages.battery={volts:null,status:'invalid',age_ms:0};await refresh();
  check(element('batteryVoltage').textContent==='—','invalid battery');
  mockVoltages.battery={volts:0,status:'ok',age_ms:0};await refresh();
  check(element('batteryVoltage').textContent==='0.0 V','valid zero voltage');
  mockVoltages.battery={volts:3.5,status:'ok',age_ms:2499};await refresh();await wait(300);
  check(element('batteryVoltage').textContent==='—','browser expires old voltage');
  mockVoltageFail=true;await refresh();check(element('batteryVoltage').textContent==='—'&&!button.disabled,'voltage endpoint failure isolates relay');
  mockVoltageFail=false;
  button.click();check(button.disabled&&!current.on,'no optimistic on');
  await wait(150);check(current.on&&current.saved_on&&!button.disabled,'confirmed on');
  button.click();await wait(150);check(!current.on&&writeCalls===2,'confirmed off');
  mockFail=true;await refresh();check(button.disabled&&current===null,'offline disables');
  check(element('batteryVoltage').textContent==='—'&&element('mainsVoltage').textContent==='—','offline clears voltages');
  check(element('zigbeeJoin').disabled&&element('zigbeeReset').disabled,'offline disables pairing');
  mockFail=false;mockState.on=true;mockState.saved_on=true;
  await refresh();check(current.on&&!button.disabled,'reconnect restores from device');
  element('webPassword').value='new-password';element('webPasswordConfirm').value='typo';
  submit('settingsForm');await wait(100);check(settingsWrites===0,'password confirmation');
  element('webUsername').value='owner';element('webPasswordConfirm').value='new-password';
  submit('settingsForm');await wait(100);check(!signedIn&&!mockAuthorized,'credential change logs out');
  await signIn('admin','admin');check(!signedIn,'old credentials rejected');
  await signIn('owner','new-password');check(signedIn,'new credentials accepted');
  element('ssid').value='My B-T HOME';element('wifiPassword').value='wifi-new-123';
  submit('settingsForm');await wait(100);
  check(!signedIn&&notice.textContent.includes('My B-T HOME'),'Wi-Fi reconnect notice');
  check(mockWifiPassword==='wifi-new-123','Wi-Fi password changed');
  await signIn('owner','new-password');check(element('ssid').value==='My B-T HOME','settings restored');
  check(!element('webPassword').value&&!element('wifiPassword').value,'password fields cleared');
  element('primarySsid').value='Primary router';element('primaryPassword').value='primary-pass';
  element('backupSsid').value='Backup router';element('backupOpen').checked=true;
  element('webUsername').value='Unsaved owner';
  submit('routerForm');await wait(100);
  check(!('ssid' in lastSettingsWrite)&&!('username' in lastSettingsWrite)&&mockSettings.username==='owner','router save isolates access');
  check(element('primarySsid').closest('form').id==='routerForm'&&element('routerPanel').closest('article').previousElementSibling.contains(element('settingsPanel')),'separate router section follows access');
  check(!document.querySelector('footer'),'footer removed');
  check(lastSettingsWrite.primary_ssid==='Primary router'&&lastSettingsWrite.primary_password==='primary-pass','primary credentials submitted');
  check(lastSettingsWrite.backup_ssid==='Backup router'&&lastSettingsWrite.backup_open,'open backup submitted');
  check(!element('primaryPassword').value&&!element('backupPassword').value,'station secrets cleared');
  await loadSettings();
  check(element('primarySsid').value==='Primary router'&&element('backupOpen').checked,'station settings restored');
  check(element('relayMode').value==='1'&&!element('saveRelayMode').disabled,'manual relay default');
  for(const mode of [0,2,1]){
   element('relayMode').value=String(mode);element('relayMode').dispatchEvent(new Event('change'));
   submit('relayModeForm');await wait(100);
   check(mockState.mode===mode&&button.disabled===(mode!==1),'relay mode controls manual button');
   check(element('relayMode').value===String(mode)&&!element('relayMode').disabled,'relay mode confirmation');
  }
  check(element('outageGroup').value==='2','outage default group');
  element('outageGroup').value='0';submit('outageForm');await wait(100);
  check(mockOutages.group===0&&outageWrites===1,'disable outage schedule');
  element('outageGroup').value='12';submit('outageForm');await wait(100);
  check(mockOutages.group===12&&outageWrites===2,'maximum outage group');
  for(const value of ['13','-1','1.5','']){
   element('outageGroup').value=value;submit('outageForm');await wait(30);
   check(outageWrites===2,'invalid outage group rejected');
  }
  mockOutageFail=true;element('outageGroup').value='2';submit('outageForm');await wait(100);
  check(mockOutages.group===12&&element('outageMessage').classList.contains('error'),'outage save failure');
  mockOutageFail=false;await loadOutages();
  check(element('outageGroup').value==='12','outage saved group restored');
  mockSaveFail=true;element('ssid').value='Unsaved';submit('settingsForm');await wait(100);
  check(mockSettings.ssid==='My B-T HOME'&&element('settingsMessage').classList.contains('error'),'save failure');
  mockSaveFail=false;mockStorageFail=true;button.click();await wait(150);
  check(button.disabled&&!current.storage_known&&current.on,'relay storage error');
  mockAuthorized=false;await refresh();check(!signedIn&&app.hidden,'expired session');
  await signIn('owner','new-password');element('logout').click();await wait(100);
  check(!mockAuthorized&&app.hidden&&!loginCard.hidden,'logout');
  check(element('batteryVoltage').textContent==='—','logout clears voltages');
  await signIn('owner','new-password');await loadOta();
  check(!element('otaCheck').disabled&&element('otaInstall').disabled,'OTA idle');
  element('otaCheck').click();await wait(100);
  check(otaWrites===1&&!element('otaInstall').disabled,'OTA check');
  mockConfirm=false;element('otaInstall').click();await wait(30);
  check(otaWrites===1,'OTA confirmation cancelled');
  mockConfirm=true;element('otaInstall').click();await wait(100);
  check(otaWrites===2&&element('otaInstall').disabled&&element('otaCheck').disabled,'OTA install busy');
  mockOta.received=500;await loadOta();
  check(element('otaProgress').value===50&&!element('otaProgress').hidden,'OTA progress');
  mockOta.state='error';mockOta.error='checksum_failed';mockOta.busy=false;await loadOta();
  check(element('otaMessage').classList.contains('error')&&!element('otaCheck').disabled,'OTA error retry');
  mockOta.connected=false;await loadOta();check(element('otaCheck').disabled,'OTA no Wi-Fi');
  mockOta.connected=true;mockOta.supported=false;await loadOta();check(element('otaCheck').disabled,'OTA unsupported');
  mockAuthorized=false;await loadOta();check(app.hidden,'OTA authentication expiry');
  document.body.dataset.testResult='PASS';
 }catch(error){document.body.dataset.testResult='FAIL: '+error.message;}
})();
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--browser", required=True, help="Path to a Chromium or Edge executable")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    build = root / "build" / "web-smoke"
    build.mkdir(parents=True, exist_ok=True)
    page = (root / "components/bth_web/web/index.html").read_text(encoding="utf-8")
    # Checks drive refresh explicitly; background polls can race expiry assertions.
    page = page.replace("poll();})();", "})();")
    page = page.replace("<script>", "<script>" + MOCK + "</script><script>", 1)
    page = page.replace("</body>", "<script>" + CHECKS + "</script></body>")
    harness = build / "index.html"
    harness.write_text(page, encoding="utf-8")
    result = subprocess.run([
        args.browser, "--headless", "--disable-gpu", "--disable-background-networking",
        "--no-first-run", "--no-default-browser-check",
        "--user-data-dir=" + str(build / "profile"), "--virtual-time-budget=8000",
        "--dump-dom", harness.as_uri(),
    ], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=45)
    (build / "result.html").write_text(result.stdout, encoding="utf-8")
    (build / "browser.log").write_text(result.stderr, encoding="utf-8")
    if result.returncode != 0 or 'data-test-result="PASS"' not in result.stdout:
        print("FAIL: inspect build/web-smoke/result.html and browser.log", file=sys.stderr)
        return 1
    print("PASS: voltage readings, freshness, login, settings, reconnect, relay, OTA and error UI")
    return 0


if __name__ == "__main__":
    sys.exit(main())
