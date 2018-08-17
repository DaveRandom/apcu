<?php

/* based on sapi/cli/tests/php_cli_server.inc */
const PHP_CLI_SERVER_HOSTNAME = '127.0.0.1';
const PHP_CLI_SERVER_PORT = 8000 + (PHP_INT_SIZE * 100) + (PHP_MAJOR_VERSION * 10) + PHP_MINOR_VERSION;
const PHP_CLI_SERVER_ADDRESS = PHP_CLI_SERVER_HOSTNAME . ":" . PHP_CLI_SERVER_PORT;

final class ServerTestGlobals
{
	const IS_WINDOWS = (PHP_OS & "\xDF\xDF\xDF") === 'WIN';

	public static $doc_root = __DIR__;
    public static $router = "index.php";
	public static $handles = [];
	public static $num_servers = 3;

    public static function router_full_path()
    {
    	return self::$doc_root . DIRECTORY_SEPARATOR . self::$router;
    }
}

function server_start_one($host, $port, $php_opts = [], $no_router = false)
{
	$php_executable = getenv('TEST_PHP_EXECUTABLE');

	$descriptorspec = [
		0 => STDIN,
		1 => STDOUT,
		2 => STDERR,
	];

	$php_args = getenv('TEST_PHP_ARGS');
	if (empty($php_args)) {
		$ext = ServerTestGlobals::IS_WINDOWS ? 'php_apcu.dll' : 'apcu.so';
		if (ServerTestGlobals::IS_WINDOWS) {
			$part0 = 8 == PHP_INT_SIZE ? "x64" : "";
			$part1 = ZEND_DEBUG_BUILD ? "Debug" : "Release";
			$part1 = PHP_ZTS ? ($part1 . "_TS") : $part1;
			$php_args = sprintf("-d extension_dir=%s/../%s", ServerTestGlobals::$doc_root, "{$part0}/{$part1}");
		} else {
			$php_args = sprintf("-d extension_dir=%s/../modules", ServerTestGlobals::$doc_root);
		}
		$php_args = "$php_args -d extension=$ext";
	}
	if ($php_opts) {
		$php_args = "$php_args -d " . implode(' -d ', $php_opts);;
	}

	if (substr(PHP_OS, 0, 3) == 'WIN') {
		$cmd = "{$php_executable} -n {$php_args} -t " . ServerTestGlobals::$doc_root . " -S {$host}:{$port}";
		if (!$no_router) {
			$cmd .= " " . ServerTestGlobals::$router;
		}

		$handle = proc_open(addslashes($cmd), $descriptorspec, $pipes, ServerTestGlobals::$doc_root, null, ["bypass_shell" => true, "suppress_errors" => true]);
	} else {
		$cmd = "exec {$php_executable} -n {$php_args} -t " . ServerTestGlobals::$doc_root . " -S {$host}:{$port}";
		if (!$no_router) {
			$cmd .= " " . ServerTestGlobals::router_full_path();
		}
		$cmd .= " 2>/dev/null";

		$handle = proc_open($cmd, $descriptorspec, $pipes, ServerTestGlobals::$doc_root);
	}

	// note: even when server prints 'Listening on localhost:8964...Press Ctrl-C to quit.'
	//       it might not be listening yet...need to wait until fsockopen() call returns
	for ($i = 0; $i < 10 && !connection_test($host, $port); $i++) {
		usleep(100000);
	}

	return $handle;
}

function server_start($code = 'echo "Hello world";', $php_opts = [], $no_router = false)
{
	if ($code) {
		file_put_contents(ServerTestGlobals::router_full_path(), '<?php ' . $code . ' ?>');
	}

	for ($i = 0; $i < ServerTestGlobals::$num_servers; $i++) {
		ServerTestGlobals::$handles[] = server_start_one(PHP_CLI_SERVER_HOSTNAME, PHP_CLI_SERVER_PORT + $i, $php_opts, $no_router);
	}

	register_shutdown_function(function() {
		foreach (ServerTestGlobals::$handles as $handle) {
			proc_terminate($handle);
		}
		@unlink(ServerTestGlobals::router_full_path());
	});
	// don't bother sleeping, server is already up
	// server can take a variable amount of time to be up, so just sleeping a guessed amount of time
	// does not work. this is why tests sometimes pass and sometimes fail. to get a reliable pass
	// sleeping doesn't work.
}

function get_response($fp, $data_only = true)
{
	$s = '';

	while (!feof($fp)) {
		$s .= fgets($fp);
	}

	if ($data_only) {
		$parts = explode("\r\n\r\n", $s);
		$s = $parts[1];
	}

	return $s;
}


function connection_test($host, $port)
{
	$port = intval($port) ?: 80;

	$fp = @fsockopen($host, $port, $errno, $errstr, 10);
	if (!$fp) {
		return false;
	}

	$send = "GET / HTTP/1.1\nHost: {$host}\r\n\r\n";

	/* will not out here, just test if the connection has worked*/
	if (@fwrite($fp, $send)) {
		get_response($fp);
		fclose($fp);

		return true;
	}

	@fclose($fp);

	return false;
}

function run_test_simple($request_uri = null)
{
	$send = "GET /" . $request_uri . " HTTP/1.1\nHost: " . PHP_CLI_SERVER_HOSTNAME . "\r\n\r\n";

	for ($i = 0; $i < ServerTestGlobals::$num_servers; $i++) {
		run_test(PHP_CLI_SERVER_HOSTNAME, PHP_CLI_SERVER_PORT + $i, $send);
	}
}

function run_test($host, $port, $send)
{
	$fp = fsockopen($host, $port, $errno, $errstr, 10);
	if (!$fp) {
		die(sprintf("connect failed errno=%d errstr='%s'", $errno, $errstr));
	}

	if (fwrite($fp, $send)) {
		echo get_response($fp);
	}

	fclose($fp);
}
