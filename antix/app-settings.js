/* Schema forms are upstream JSON Editor; persistence uses ordinary app JSON files. */
let appSettingsEditor=null, appSettingsDocument=null, editorLibrary=null;
async function openAppSettings(packageName){
 try{
  const document=await get('/api/apps/settings?package='+encodeURIComponent(packageName));
  if(!editorLibrary)editorLibrary=new Promise((resolve,reject)=>{
   const script=window.document.createElement('script');script.src='/assets/jsoneditor.min.js';
   script.onload=resolve;script.onerror=()=>{editorLibrary=null;script.remove();reject(Error('Could not load settings editor.'));};
   window.document.head.append(script);
  });
  await editorLibrary;
  if(appSettingsEditor)appSettingsEditor.destroy();
  appSettingsDocument=document;
  $('app-settings-title').textContent=document.schema.title||packageName;
  $('app-settings-path').textContent=document.path;
  $('app-settings-panel').hidden=false;
  $('app-settings-errors').textContent='';$('app-settings-save').disabled=true;
  appSettingsEditor=new JSONEditor($('app-settings-fields'),{
   schema:document.schema,startval:document.value,theme:'html',
   disable_edit_json:true,disable_properties:true,no_additional_properties:true,
   disable_collapse:true,show_errors:'always',ajax:false
  });
  const validate=()=>{
   const errors=appSettingsEditor.validate();
   $('app-settings-save').disabled=errors.length>0;
   $('app-settings-errors').textContent=errors.map(e=>e.path+': '+e.message).join('\n');
  };
  appSettingsEditor.on('ready',validate);appSettingsEditor.on('change',validate);
  $('app-settings-panel').scrollIntoView({block:'start'});
 }catch(e){message(e.message,true);}
}
function closeAppSettings(){
 if(appSettingsEditor)appSettingsEditor.destroy();
 appSettingsEditor=null;appSettingsDocument=null;$('app-settings-panel').hidden=true;
}
window.addEventListener('DOMContentLoaded',()=>{
 $('app-settings-close').onclick=closeAppSettings;
 $('app-settings-form').onsubmit=async e=>{
  e.preventDefault();if(!appSettingsEditor||appSettingsEditor.validate().length)return;
  $('app-settings-save').disabled=true;
  try{
   appSettingsDocument=await postJSON('/api/apps/settings',{
    package:appSettingsDocument.package,revision:appSettingsDocument.revision,value:appSettingsEditor.getValue()
   });
   message('Settings saved. Reopen the app to apply changes.');
  }catch(error){message(error.message,true);}
  finally{if(appSettingsEditor)$('app-settings-save').disabled=false;}
 };
});
async function postJSON(url,value){
 const response=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(value)});
 if(!response.ok)throw Error(await response.text());return response.json();
}
