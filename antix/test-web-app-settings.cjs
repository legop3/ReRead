// Real Reader check; restores the original Booru configuration in finally.
// NODE_PATH=build/web-tests/node_modules node antix/test-web-app-settings.cjs
const {chromium}=require('playwright');
const assert=require('node:assert/strict');
(async()=>{
 const browser=await chromium.launch({headless:true,executablePath:'/usr/bin/chromium-browser',args:['--no-sandbox']});
 const context=await browser.newContext();const request=context.request,base=process.argv[2]||'http://192.168.7.1';
 const url=base+'/api/apps/settings',query=url+'?package=reread-app-booru';let original;
 try{
  const initial=await request.get(query);assert(initial.ok(),await initial.text());original=await initial.json();
  const page=await context.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.goto(base+'/#apps');
  await page.locator('#apps-list article').filter({has:page.locator('h3',{hasText:'Booru'})}).getByRole('button',{name:'Settings',exact:true}).click();
  const field=page.locator('[data-schemapath="root.hosts.0.searches.0"] input');await field.waitFor();
  const test='reread_configuration_test rating:s';await field.fill(test);
  assert.equal(await page.locator('#app-settings-fields input[type=password]').count(),original.value.hosts.length);
  assert.equal(await page.locator('#app-settings-fields input[type=checkbox]').count(),0,'Button-friendly options should not appear here');
  await page.locator('#app-settings-save').click();
  await page.getByText('Settings saved. Reopen the app to apply changes.',{exact:true}).waitFor();
  const current=await (await request.get(query)).json();assert.equal(current.value.hosts[0].searches[0],test);
  const stale=await request.post(url,{data:original});assert.equal(stale.status(),400);
  const invalid=await request.post(url,{data:{...current,value:{hosts:[]}}});assert.equal(invalid.status(),400);
  await page.locator('#app-settings-close').click();
  await page.locator('#apps-list article').filter({has:page.locator('h3',{hasText:'Booru'})}).getByRole('button',{name:'Settings',exact:true}).click();
  await field.waitFor();assert.equal(await field.inputValue(),test);
  assert.deepEqual(errors,[]);
  console.log('PASS: installed-app discovery, offline nested schema form, password fields, save/reopen, stale/invalid save rejection; no Reader-toggle duplication');
 }finally{
  if(original){const latest=await (await request.get(query)).json();const restored=await request.post(url,{data:{...latest,value:original.value}});assert(restored.ok(),'Restore original app settings');}
  await browser.close();
 }
})().catch(e=>{console.error(e);process.exit(1)});
