const { Worker, isMainThread } = require('worker_threads');


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


function doWork0(){
  return `Primes from 1 to 100 are: ${findPrimes(1,100)}`;
}


function doWork1 () {
  return `The sqrt of 2 is ${Math.sqrt(2)} `;
}


if (isMainThread) {
  // This re-loads the current file inside a Worker instance.
  debugger;
  const addon = require('./build/Debug/mutate');

  let count = 0;

setInterval( () => {
  if(count++ === 0) {
    //Define a couple of functions in Isolates 0 and 1 (while running in a std::thread)
    addon.enterIsoFromOrdinaryThread(findPrimes.toString(), 0);
    addon.enterIsoFromOrdinaryThread(doWork1.toString(), 1);
    firsTime = false;
  }

  if(count & 1) {
    console.log('\nDoing JS work on std thread:');
    addon.enterIsoFromOrdinaryThread('doWork1()', 1);
  } else {
    console.log('\nAdding work to queue:');
    addon.queWorkerAction("`Primes from 1 to 100 are: ${findPrimes(1,100)}`");
  }

}, 1000);

  new Worker(__filename);
  new Worker(__filename);
  //addon.enterIso0FromOrdinaryThread(findPrimes.toString());
} else {
  const addon = require('./build/Debug/mutate');
  const index = addon.onWorkerStart();
  console.log(`Inside Worker ${index}!`);
  console.log(isMainThread);  // Prints 'false'.
  for(;;) {
    addon.waitForTask();
  }
}
