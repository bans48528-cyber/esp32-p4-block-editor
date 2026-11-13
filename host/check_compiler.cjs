const fs=require('fs'),cp=require('child_process'),path=require('path');
const base=process.argv[2]||'D:/BaiduSyncdisk/2026/8月/应用构思/visual-programming-cards';
const generate=require(path.resolve(base,'program-codegen.cjs'));
const compiler=require('E:/Spark AI 1.1.9/Spark-AI/resources/app.asar.unpacked/node_modules/pika-bytecode-gen-napi/build/Release/pika_bytecode_gen.node');
const dir=path.join(require('os').tmpdir(),'board-compiler-fixtures');fs.mkdirSync(dir,{recursive:true});
const ids=['','motor-forward','wait-time','loop-count','motor-reverse','motor-stop','combo-forward','combo-backward','combo-turn-left','combo-turn-right','combo-stop','ultrasonic-sensor','grayscale-sensor','button-sensor','host-button','loop','play-note','matrix-display','motor-power','motor-forward-continuous','motor-reverse-continuous','combo-power','combo-continuous','combo-direction'];
function web(n,all){const [t,parent,order,v,port,o,m]=n;return {id:ids[t],params:{mode:['3','0','1','2'][o],port:(t===11||t===12||t===13?'ABCD':'EFGH')[port],duration:v/10,count:v,power:String((o+1)*25),direction:['advance','retreat','left','right'][o],operator:['<','>','=='][o],distance:v,value:v,state:o?'松开':'按下',button:o?'右键':'左键',note:String(o+1),beats:['0.25','0.5','1','2'][v],pattern:Array.from({length:25},(_,i)=>(m>>>i)&1)},children:all.filter(x=>x[1]===all.indexOf(n)).sort((a,b)=>a[2]-b[2]).map(x=>web(x,all))};}
let cases=[];
for(let t=1;t<=23;t++){let v=t===16?3:([1,2,3,4,6,7,8,9,11,12].includes(t)?10:0),o=t===16?6:t===18||t===21?3:t===22?2:0;cases.push([[t,-1,0,v,3,o,0x1555555]]);}
cases.push([[3,-1,0,3,0,0,0],[1,0,0,25,2,0,0],[15,0,1,0,0,0,0],[16,2,0,1,0,4,0],[20,-2,0,0,1,0,0]]);
cases.push(Array.from({length:100},(_,i)=>[2,-1,i,9999,0,0,0]));
for(let o=0;o<4;o++)cases.push([[23,-1,0,0,0,o,0],[6,-1,1,10,0,0,0]]);
cases.push(Array.from({length:100},(_,i)=>[17,-1,i,0,0,0,(i*73411)&0x1ffffff]));
for(let i=0;i<cases.length;i++) {
 const nodes=cases[i],buf=Buffer.alloc(100*28);nodes.forEach((n,j)=>n.forEach((v,k)=>buf.writeInt32LE(v,j*28+k*4)));
 const input=path.join(dir,i+'.bin'),py=path.join(dir,i+'.py'),out=path.join(dir,i+'.o'),ref=path.join(dir,i+'.ref.py'),refout=path.join(dir,i+'.ref.o');
 fs.writeFileSync(input,buf);
 cp.execFileSync(path.resolve('host-build/Debug/program_test.exe'),[input,py,out]);
 const source=generate(nodes.filter(n=>n[1]===-1).sort((a,b)=>a[2]-b[2]).map(n=>web(n,nodes)));
 if(fs.readFileSync(py,'utf8')!==source)throw new Error('source mismatch '+i);
 fs.writeFileSync(ref,source);if(compiler.compile(ref,refout)!==0)throw new Error('reference compile failed');
 if(!fs.readFileSync(out).equals(fs.readFileSync(refout)))throw new Error('bytecode mismatch '+i);
}
console.log('PASS: '+cases.length+' source and bytecode fixtures match reference, covering all 23 blocks, nesting, staged exclusion and 100-block capacity');
