/* Original, vector-drawn diagrams. Render with Node, Playwright and Sharp.
 * NODE_PATH may point to a local dependency directory; CHROME_PATH is optional.
 * No device trace is implied: these are simplified semantic demonstrations.
 */
const fs = require('node:fs/promises');
const path = require('node:path');
const { chromium } = require('playwright');
const sharp = require('sharp');
const output = __dirname;
const C = { bg:'#fffefa', ink:'#172c30', muted:'#526568', line:'#d6dfda',
  teal:'#087e80', mint:'#d4eee5', gray:'#f5f4ef', amber:'#9b5712', sand:'#fff0d4' };
const esc = s => String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;');
const clamp = v => Math.max(0, Math.min(1, v));
const ease = v => { v=clamp(v); return v*v*(3-2*v); };
function text(x,y,s,size=26,color=C.ink,extra='') {
  return `<text x="${x}" y="${y}" fill="${color}" font-size="${size}" ${extra}>${esc(s)}</text>`;
}
function rect(x,y,w,h,fill=C.bg,stroke=C.line,r=14) {
  return `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${r}" fill="${fill}" stroke="${stroke}" stroke-width="1.5"/>`;
}
function line(x1,y1,x2,y2,color=C.line,width=2) {
  return `<path d="M${x1} ${y1} L${x2} ${y2}" fill="none" stroke="${color}" stroke-width="${width}"/>`;
}
function arrow(x1,y1,x2,y2,color=C.teal) {
  return `<path d="M${x1} ${y1} L${x2} ${y2}" fill="none" stroke="${color}" stroke-width="2.5" marker-end="url(#${color===C.amber?'amber':'teal'})"/>`;
}
function group(body,opacity=1,dx=0,dy=0) {
  return `<g opacity="${opacity}" transform="translate(${dx} ${dy})">${body}</g>`;
}
function base(title,step,body) {
  return `<svg xmlns="http://www.w3.org/2000/svg" width="800" height="460" viewBox="0 0 800 460" style="font-family:system-ui,-apple-system,'PingFang SC',sans-serif">
  <defs>${['teal','amber'].map(k=>`<marker id="${k}" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 Z" fill="${C[k]}"/></marker>`).join('')}</defs>
  <rect width="800" height="460" fill="${C.bg}"/>
  <circle cx="32" cy="29" r="18" fill="${C.teal}"/>${text(32,36,step+1,21,C.bg,'text-anchor="middle" font-weight="650"')}
  ${text(62,38,title,26,C.ink,'font-weight="600"')}
  ${[0,1,2,3].map(i=>rect(664+i*30,22,22,5,i<=step?C.teal:C.line,'none',2)).join('')}
  ${body}</svg>`;
}

const counterTitles=['更新前：原有值为 7','提交补丁：增加字段，修改方法','完成重定义：保留值，更新结构','再次调用：7 × 2 = 14'];
function counter(step,p=1) {
  const updated = step>2 ? 1 : step===2 ? ease(p*2) : 0;
  const travel = step===3 ? ease(p*1.7) : 0;
  let b=rect(16,76,330,242)+rect(430,76,354,242);
  b+=text(38,116,'已有 Counter 对象',25)+text(454,116,'方法 value()',25);
  b+=line(38,134,324,134)+line(454,134,762,134);
  b+=rect(38,154,286,62,C.gray,'none',9)+text(60,195,'count = 7',33,C.ink,'font-family="ui-monospace,Menlo,monospace"');
  b+=group(rect(38,230,286,64,C.mint,'none',9)+text(60,272,'label = null',30,C.teal,'font-family="ui-monospace,Menlo,monospace"'),updated,0,14*(1-updated));
  if(updated<1) b+=group(text(58,271,'只有 count 字段',22,C.muted),1-updated);
  b+=group(text(454,195,'return count;',27,C.ink,'font-family="ui-monospace,Menlo,monospace"'),1-updated);
  b+=group(text(454,195,'return count * 2;',27,C.teal,'font-family="ui-monospace,Menlo,monospace"'),updated);
  b+=arrow(608,213,608,240);
  if(step<2) b+=text(608,286,'返回 7',35,C.ink,'text-anchor="middle" font-weight="600"');
  if(step===2) b+=text(608,285,'等待再次调用',24,C.muted,'text-anchor="middle"');
  if(step===3) {
    b+=arrow(346,185,429,185);
    if(travel<1) b+=rect(335+travel*79,168,34,34,C.mint,C.teal,17)+text(352+travel*79,192,'7',23,C.teal,'text-anchor="middle"');
    b+=group(text(608,286,'返回 14',36,C.teal,'text-anchor="middle" font-weight="650"'),ease((travel-.6)*2.5));
  }
  if(step===0) b+=text(400,384,'已有数据与旧方法共同产生结果 7',25,C.muted,'text-anchor="middle"');
  if(step===1) {
    const a=ease(p*2.5);
    b+=group(rect(16,346,768,94,C.sand,'none',12)+text(36,380,'补丁 DEX',22,C.amber,'font-weight="600"')+
      text(202,380,'+ String label',27,C.ink,'font-family="ui-monospace,Menlo,monospace"')+
      text(202,419,'value() → count * 2',27,C.ink,'font-family="ui-monospace,Menlo,monospace"'),a,0,24*(1-a));
  }
  if(step===2) {
    b+=group(arrow(174,353,174,319)+arrow(608,353,608,319),1-updated);
    b+=text(181,381,'原值保留 · 新字段为 null',23,C.teal,'text-anchor="middle"');
    b+=text(608,381,'方法实现已更新',23,C.teal,'text-anchor="middle"');
    b+=text(400,429,'已有对象不会重新执行构造器',21,C.muted,'text-anchor="middle"');
  }
  if(step===3) {
    b+=rect(16,345,768,93,C.mint,'none',12)+text(400,384,'原来的对象，保留原来的 count',26,C.ink,'text-anchor="middle"');
    b+=text(400,420,'后续调用执行新方法',25,C.teal,'text-anchor="middle"');
  }
  return base(counterTitles[step],step,b);
}

const retryTitles=['旧调用仍在执行','第一次请求：拒绝提交','旧调用结束，活动帧退出','第二次请求：重新校验后提交'];
function retry(step,p=1) {
  const exit=step===2?ease(p*1.6):step>2?1:0;
  const commit=step===3?ease(p*2):0;
  const oldProgress=step===0?.08+p*.2:step===1?.28+p*.2:step===2?.48+.52*ease(p*1.8):1;
  let b=rect(16,76,330,255)+rect(430,76,354,255);
  b+=text(38,115,'应用线程',25)+text(454,115,'重定义请求',25);
  b+=line(38,133,324,133)+line(454,133,762,133);
  // The lower caller remains in place as the active callee leaves the stack.
  b+=rect(38,253,286,55,C.gray,'none',8)+text(181,289,'调用者',24,C.muted,'text-anchor="middle"');
  const old=rect(38,155,286,82,C.gray,C.line,9)+text(58,190,'旧 value() · V1',27)+
    rect(58,209,246,7,C.line,'none',3)+rect(58,209,246*oldProgress,7,C.amber,'none',3);
  b+=group(old,1-exit,0,-70*exit);
  if(step===2) b+=group(text(181,202,'旧调用已结束',25,C.muted,'text-anchor="middle"'),exit);
  if(step===3) b+=group(rect(38,155,286,82,C.mint,C.teal,9)+text(181,190,'新调用 · V2',27,C.teal,'text-anchor="middle"')+
    text(181,219,'使用新实现',21,C.teal,'text-anchor="middle"'),ease((p-.6)*3));
  if(step<=1){
    const y=step===0?14*(1-ease(p*3)):0;
    b+=group(rect(454,155,306,60,C.sand,'none',9)+text(607,194,'补丁 V2',28,C.amber,'text-anchor="middle"'),1,0,y);
    b+=arrow(607,224,607,249,step===1?C.amber:C.teal);
    b+=text(607,286,step===1?'× 本次未提交':'检查当前活动调用',step===1?29:23,step===1?C.amber:C.muted,'text-anchor="middle"');
  }
  if(step===1){
    b+=line(607,243,607,255,C.amber,3)+line(576,251,638,251,C.amber,4);
  }
  if(step<=1) b+=arrow(428,182,348,182,step===1?C.amber:C.teal)+text(388,213,step===1?'无法恢复':'检查旧帧',18,step===1?C.amber:C.muted,'text-anchor="middle"');
  if(step===2) b+=text(607,193,'等待新的请求',26,C.muted,'text-anchor="middle"')+text(607,267,'V2 尚未生效',25,C.muted,'text-anchor="middle"');
  if(step===3) {
    b+=group(rect(454,155,306,60,C.mint,'none',9)+text(607,194,'重新提交 V2',28,C.teal,'text-anchor="middle"'),1,0,10*(1-commit));
    b+=arrow(607,224,607,248)+group(text(607,286,'✓ 全部校验通过',27,C.teal,'text-anchor="middle"'),commit);
  }
  const newVersion=step===3&&commit===1;
  b+=rect(16,351,768,91,newVersion?C.mint:C.gray,'none',12);
  b+=text(38,389,'当前生效版本',22,C.muted)+text(234,390,newVersion?'V2':'V1',32,newVersion?C.teal:C.ink,'font-weight="650"');
  b+=text(38,424,[ '橙色进度条：正在执行的旧调用', '旧帧无法即时恢复 → 拒绝补丁，保留 V1', '退出后仍需重新发起请求', '后续新调用使用 V2' ][step],23,newVersion?C.teal:C.muted);
  return base(retryTitles[step],step,b);
}

async function main() {
  const browser=await chromium.launch({headless:true,executablePath:process.env.CHROME_PATH});
  const page=await browser.newPage({viewport:{width:800,height:460},deviceScaleFactor:1});
  const capture=async svg=>{
    await page.setContent(`<style>html,body{margin:0;width:800px;height:460px;overflow:hidden}</style>${svg}`);
    await page.evaluate(()=>document.fonts.ready);
    return page.screenshot();
  };
  try {
    for (const [name,draw] of [['counter-state',counter],['active-call-retry',retry]]) {
      const frames=[],delay=[];
      for(let step=0;step<4;step++) {
        const svg=draw(step,1);
        await fs.writeFile(path.join(output,`${name}-step-${step+1}.svg`),svg);
        await fs.writeFile(path.join(output,`${name}-step-${step+1}.png`),await capture(svg));
        for(let f=0;f<=12;f++){
          frames.push(await capture(draw(step,f/12)));
          delay.push(f===12?2100:80);
        }
      }
      await sharp(frames,{join:{animated:true}}).gif({delay,loop:1,colours:128,dither:0,effort:7}).toFile(path.join(output,`${name}.gif`));
      console.log(`${name}: ${frames.length} frames, ${delay.reduce((a,b)=>a+b)} ms`);
    }
  } finally { await browser.close(); }
}
main().catch(e=>{console.error(e);process.exitCode=1;});
