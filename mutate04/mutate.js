const { Worker, isMainThread } = require('worker_threads');


function doWork0(){

  function findPrimes(fromNumber, toNumber) {
    // Create an array containing all integers
    // between the two specified numbers.
    var list = [];
    for (var i=fromNumber; i<=toNumber; i++) {
      if (i>1) list.push(i);
    }
  
    // Test for primes.
    var maxDiv = Math.round(Math.sqrt(toNumber));
    var primes = [];
  
    for (var i=0; i<list.length; i++) {
      var failed = false;
      for (var j=2; j<=maxDiv; j++) {
        if ((list[i] != j) && (list[i] % j == 0)) {
          failed = true;
        } else if ((j==maxDiv) && (failed == false)) {
          primes.push(list[i]);
        }
      }
    }
    return primes;
  }
  
  return `Primes from 1 to 100 are: ${findPrimes(1,100)}`;
}

const doWork0Str = doWork0.toString() + 'doWork0()';


function doWork1 () {
  return `The sqrt of 2 is ${Math.sqrt(2)} `;
}

const doWork1Str = doWork1.toString() + 'doWork1()';

if (isMainThread) {
  // This re-loads the current file inside a Worker instance.
  debugger;
  const addon = require('./build/Debug/mutate');


//addon.start(obj);

setInterval( () => {
  //addon.let_worker_work();
  //addon.enterIso0("console.log('Entered Iso0 from main thread');");
  //addon.enterIso0("throw Error('Exception from Iso0!');");
  addon.queWorkerAction(doWork1Str);
  addon.enterIso0FromOrdinaryThread(doWork0Str);
}, 1000);

  new Worker(__filename);
  new Worker(__filename);
} else {
  const addon = require('./build/Debug/mutate');
  addon.onWorkerStart();
  console.log('Inside Worker!');
  console.log(isMainThread);  // Prints 'false'.
  for(;;) {
    addon.waitForTask();
  }
}
