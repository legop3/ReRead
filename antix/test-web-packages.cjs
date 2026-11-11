// Browser checks for online package controls. Run with Playwright available to Node.
const {chromium} = require('playwright');
const {readFileSync} = require('node:fs');
const assert = require('node:assert/strict');

(async () => {
  const browser = await chromium.launch({headless:true, executablePath:process.env.CHROMIUM_PATH || '/usr/bin/chromium-browser', args:['--no-sandbox']});
  try {
    const page = await browser.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    const state = {version:'0.12.0', apps:[{package:'reread-app-camera', name:'Camera', version:'0.4.0', description:'Camera', service:false}], job:{state:'complete'}, log:'', reboot_required:false,
      catalog:{checked:1794200000, packages:[{package:'reread', version:'0.13.0', upgrade:true}, {package:'reread-app-camera', version:'0.5.0', upgrade:true}, {package:'reread-app-notes', version:'1.0', description:'Notes', installed:null, upgrade:false}]}};
    const requests = [];
    await page.route('http://reader.test/**', async route => {
      const request = route.request(), url = new URL(request.url());
      if (url.pathname === '/') return route.fulfill({contentType:'text/html', body:readFileSync(process.env.REREAD_WEB_HTML || __dirname+'/web.html','utf8')});
      if (url.pathname === '/assets/app-settings.js') return route.fulfill({contentType:'text/javascript',body:readFileSync(__dirname+'/app-settings.js','utf8')});
      if (request.method() === 'POST') {
        requests.push([url.pathname, request.postDataJSON()]);
        return route.fulfill({json:{state:'running'}});
      }
      const data = {'/api/platform':state, '/api/status':{hostname:'reader',version:state.version,free:123,total:456,kernel:'test',files:[]}, '/api/battery':{capacity_percent:90,monitor_running:true},
        '/api/wifi':{interface:'wlan0',state:'COMPLETED',ssid:'Home',address:'192.168.0.2',networks:[],profiles:[{id:'0',ssid:'Home',enabled:true,current:true},{id:'1',ssid:'Guest',enabled:false,current:false}]},
        '/api/wifi/profile':{id:'1',ssid:'Guest',enabled:false,hidden:false,priority:5,security:'open'},
        '/api/repositories':[{id:'reread.sources',path:'/etc/apt/sources.list.d/reread.sources',apps:true,enabled:true,content:'Types: deb\nURIs: https://example.org/apt\nSuites: stable\nComponents: main\n'}, {id:'debian.list',path:'/etc/apt/sources.list.d/debian.list',apps:false,enabled:true,content:'deb https://deb.debian.org/debian trixie main\n'}]}[url.pathname];
      assert(data, url.pathname);
      return route.fulfill({json:data});
    });
    await page.goto('http://reader.test/#updates');
    await page.locator('#update-online').waitFor({state:'visible'});
    assert.match(await page.locator('#online-version').innerText(), /0.13.0/);
    assert.equal(requests.length, 0, 'Loading a page must not start a network check');
    await page.locator('#check-updates').click();
    await page.waitForFunction(() => document.querySelector('#package-status').textContent !== '');
    await page.waitForTimeout(100);
    assert.equal(requests[0][0], '/api/packages/check');
    await page.locator('#update-online').click();
    await page.waitForTimeout(100);
    assert.deepEqual(requests[1], ['/api/packages/install',{package:'reread'}]);
    await page.locator('nav a[href="#apps"]').click();
    await page.locator('#available-apps button').waitFor();
    assert.match(await page.locator('#available-apps').innerText(), /Notes/);
    await page.locator('#available-apps button').click();
    await page.waitForTimeout(100);
    assert.deepEqual(requests[2], ['/api/packages/install',{package:'reread-app-notes'}]);
    await page.locator('#apps-list button').filter({hasText:'Update to 0.5.0'}).click();
    await page.waitForTimeout(100);
    assert.deepEqual(requests[3], ['/api/packages/install',{package:'reread-app-camera'}]);
    state.job = {state:'running',message:'Installing'};
    await page.evaluate(() => refreshPlatform());
    assert(await page.locator('#check-apps').isDisabled());
    assert(await page.locator('#available-apps button').isDisabled());
    state.job = {state:'failed',message:'Repository check failed. Previous results are still shown.'};
    await page.evaluate(() => refreshPlatform());
    assert.match(await page.locator('#package-status').innerText(), /failed/);
    assert.match(await page.locator('#available-apps').innerText(), /Notes/);
    assert.equal(await page.locator('#check-apps').isDisabled(), false);
    await page.locator('nav a[href="#wifi"]').click();
    await page.waitForFunction(() => document.querySelectorAll('#wifi-profiles th').length === 2);
    const guest = page.locator('#wifi-profiles tr').filter({has:page.locator('th',{hasText:'Guest'})});
    await guest.getByRole('button',{name:'Edit',exact:true}).click();
    await page.waitForFunction(() => document.querySelector('#wifi-form').dataset.id === '1');
    assert.equal(await page.locator('#wifi-password').inputValue(),'');
    assert.equal(await page.locator('#wifi-enabled').isChecked(),false);
    await page.locator('#wifi-priority').fill('25');
    await page.locator('#save-network').click();
    await page.waitForFunction(() => !document.querySelector('#wifi-form').dataset.id);
    assert.equal(requests.at(-1)[0],'/api/wifi/save');
    assert.equal(requests.at(-1)[1].id,'1');
    assert.equal(requests.at(-1)[1].priority,25);
    assert.equal(requests.at(-1)[1].connect,false);
    await page.locator('#new-network').click();
    await page.locator('#ssid').fill('Travel');
    await page.locator('#wifi-password').fill('example-password');
    await page.locator('#save-connect').click();
    await page.waitForTimeout(100);
    assert.equal(requests.at(-1)[1].id,null);
    assert.equal(requests.at(-1)[1].connect,true);
    await guest.getByRole('button',{name:'Connect',exact:true}).click();
    await page.waitForTimeout(100);
    assert.deepEqual(requests.at(-1),['/api/wifi/action',{id:'1',action:'connect'}]);
    await page.locator('a[href="#repositories"]').click();
    await page.locator('#app-repos th').waitFor();
    await page.locator('#app-repos').getByRole('button',{name:'Disable',exact:true}).click();
    await page.waitForTimeout(100);
    assert.deepEqual(requests.at(-1),['/api/repositories',{action:'toggle',id:'reread.sources',enabled:false}]);
    await page.locator('#app-repos').getByRole('button',{name:'Edit',exact:true}).click();
    await page.locator('#repo-content').fill('Types: deb\nURIs: https://new.example/apt\nSuites: stable\nComponents: main\n');
    await page.locator('#repo-edit button[type=submit],#repo-edit button:not([type])').click();
    await page.waitForTimeout(100);
    assert.equal(requests.at(-1)[1].action,'save');
    assert.match(requests.at(-1)[1].content,/new.example/);
    assert(await page.locator('#repo-name').isVisible(), 'Repository form is always visible');
    await page.locator('#repo-name').fill('community');
    await page.locator('#repo-uri').fill('https://community.example/apt');
    await page.locator('#repo-signed-by').fill('/usr/share/keyrings/community.asc');
    await page.locator('#repo-add button').click();
    await page.waitForTimeout(100);
    assert.equal(requests.at(-1)[1].action,'add');
    assert.equal(requests.at(-1)[1].name,'community');
    page.once('dialog',dialog=>dialog.accept());
    await page.locator('#app-repos').getByRole('button',{name:'Remove',exact:true}).click();
    await page.waitForTimeout(100);
    assert.equal(requests.at(-1)[1].action,'remove');
    assert.deepEqual(errors, []);
    console.log('PASS: update/app controls, busy state, cached results after failure, no automatic checks, Wi-Fi profile editing, and repository controls');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode=1; });
