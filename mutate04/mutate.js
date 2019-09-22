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

if (isMainThread) {
  // This re-loads the current file inside a Worker instance.
  debugger;
  const addon = require('./build/Debug/mutate');

var obj = { 	x: 1  };

//addon.start(obj);

setInterval( () => {
  //addon.let_worker_work();
  //addon.enterIso0("console.log('Entered Iso0 from main thread');");
  //addon.enterIso0("throw Error('Exception from Iso0!');");
  addon.queWorkerAction("throw Error('Exception from worker!');")
  addon.enterIso0FromOrdinaryThread("throw Error('Exception from worker!');")
	console.log(obj)
}, 1000);

  new Worker(__filename);
  new Worker(__filename);
} else {
  const addon = require('./build/Debug/mutate');
  console.log('Inside Worker!');
  addon.onWorkerStart();
  console.log(isMainThread);  // Prints 'false'.
  for(;;) {
    addon.waitForTask();
  }
}


// const addon = require('./build/Debug/mutate');

// var obj = { 	x: 1  };

// addon.start(obj);

// setInterval( () => {
// 	addon.let_worker_work();
// 	console.log(obj)
// }, 1000);
