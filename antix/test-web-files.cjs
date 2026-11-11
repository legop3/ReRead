// Real browser check against a Reader. Only creates/removes its own test folder.
// NODE_PATH=... node antix/test-web-files.cjs [http://192.168.7.1]
const {chromium}=require('playwright');
const assert=require('node:assert/strict');
(async()=>{
 const base=process.argv[2]||'http://192.168.7.1';
 const address=new URL(base);address.port='8080';address.pathname='/';
 const storage=address.href,files=storage+'files/',folder='reread-browser-test-'+Date.now();
 const browser=await chromium.launch({headless:true,executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium-browser',args:['--no-sandbox']});
 const context=await browser.newContext({viewport:{width:1280,height:900}});
 try{
  const request=context.request;
  assert((await request.fetch(files+folder+'/',{method:'MKCOL'})).ok());
  assert((await request.put(files+folder+'/hello.txt',{data:'Reader browser preview test\n'})).ok());
  const page=await context.newPage(),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto(base+'/#files');
  const frame=page.frameLocator('#file-manager');
  await frame.locator('#rr-storage a.rr-card[href="/files/"]').click();
  await frame.locator('#files').waitFor();
  await frame.locator('#files a').filter({hasText:folder}).click();
  await frame.locator('#files a').filter({hasText:'hello.txt'}).waitFor();
  await frame.locator('#opa_mkd').click();
  await frame.locator('#op_mkdir input[name="name"]').fill('Browser-created folder');
  await frame.locator('#op_mkdir input[type="submit"]').click();
  await page.waitForTimeout(1500);
  console.log('Folder after creation:',page.frames().map(f=>f.url()).join(' | '));
  const listing=await request.get(files+folder+'/?ls');
  assert.match(await listing.text(),/Browser-created/);
  await frame.locator('#files').waitFor();
  await frame.locator('#opa_up').click();
  await frame.locator('#u2form input[type=file]:not([webkitdirectory])').last().setInputFiles({name:'browser-upload.txt',mimeType:'text/plain',buffer:Buffer.from('Uploaded with the real browser widget\n')});
  await frame.locator('#modal-ok').click();
  let uploaded=false;
  for(let attempt=0;attempt<30;attempt++){
   const result=await request.get(files+folder+'/Browser-created%20folder/browser-upload.txt');
   if(result.ok() && /real browser widget/.test(await result.text())){uploaded=true;break;}
   await page.waitForTimeout(500);
  }
  if(!uploaded){console.log(await frame.locator('body').innerText());await page.screenshot({path:'build/web-tests/upload-failure.png',fullPage:true});}
  assert(uploaded,'Browser upload did not reach the shared folder');
  // A transfer remains in the same frame when the owner visits another tab.
  const source=await page.locator('#file-manager').getAttribute('src');
  await page.locator('nav a[href="#overview"]').click();
  await page.locator('nav a[href="#files"]').click();
  assert.equal(await page.locator('#file-manager').getAttribute('src'),source);
  assert.equal(await page.locator('#open-files').getAttribute('href'),storage);
  assert.equal(await page.locator('#files-root').count(),0);
  assert.match(await page.locator('#files-space').textContent(),/free/);
  // Exercise the upstream editor, not a replacement upload under a different name.
  const embedded=page.frames().find(f=>f.url().startsWith(files));
  await embedded.goto(files+folder+'/hello.txt?edit');
  const edited='Edited on ReRead — existing text file\nSecond line\n';
  await frame.locator('#mt').fill(edited);
  await frame.locator('#save').click();
  let saved=false;
  for(let attempt=0;attempt<30;attempt++){
   if(await (await request.get(files+folder+'/hello.txt')).text()===edited){saved=true;break;}
   await page.waitForTimeout(200);
  }
  assert(saved,'Editing did not replace the existing file');
  await embedded.goto(embedded.url());
  assert.equal(await frame.locator('#mt').inputValue(),edited);
  assert.deepEqual(errors,[]);
  if(process.env.SCREENSHOT)await page.screenshot({path:process.env.SCREENSHOT,fullPage:true});
  console.log('PASS: embedded browsing, folder creation, upload, text editing/save/reload, tab navigation and shared storage');
 }finally{
  await context.request.fetch(files+folder+'/',{method:'DELETE'});
  await browser.close();
 }
})().catch(error=>{console.error(error);process.exit(1)});
