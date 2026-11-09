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
      if (url.pathname === '/') return route.fulfill({contentType:'text/html', body:readFileSync(__dirname+'/web.html','utf8')});
      if (request.method() === 'POST') {
        requests.push([url.pathname, request.postDataJSON()]);
        return route.fulfill({json:{state:'running'}});
      }
      const data = {'/api/platform':state, '/api/status':{hostname:'reader',version:state.version,free:123,total:456,kernel:'test',files:[]}, '/api/battery':{capacity_percent:90,monitor_running:true}}[url.pathname];
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
    await page.locator('a[href="#apps"]').click();
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
    assert.deepEqual(errors, []);
    console.log('PASS: update/app controls, busy state, cached results after failure, and no automatic checks');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode=1; });
