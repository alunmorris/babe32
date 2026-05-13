<?php
/******************* aichat using gemini  ***************
* 130326 New https://chatgpt.com/c/69b48b9b-eec8-8392-9001-7436c0ccf4ec
* 140326 add preamble to guide responses of c.250 words, do web searches. Remove buttons, 'new' clears chat. Remove <style> section. Keep users seperate
* 150326 bot replies preserve <a> tags as clickable links instead of escaping them
* 170326 Make heasding Green
* 180326 unbold 'new' in the Help line
* 190326 Make heading font x-large
* 120526 Store API key in secrets file
* 030526 Change to Gemini 3 Flash Lite. Gemini 3.1 too slow
*/
ini_set('display_errors', 1);
ini_set('display_startup_errors', 1);
error_reporting(E_ALL);
header("Cache-Control: no-store, no-cache, must-revalidate, max-age=0");
header("Pragma: no-cache");
header("Expires: 0");

session_start();

/* ---------- USER IDENTIFICATION ---------- */
// Generate a unique ID per session if not already set
if (!isset($_SESSION['user_id'])) {
    if (function_exists('random_bytes')) {
        $_SESSION['user_id'] = bin2hex(random_bytes(8));
    } else {
        $_SESSION['user_id'] = bin2hex(openssl_random_pseudo_bytes(8));
    }
}
$user_id = $_SESSION['user_id'];

/* ---------- CONFIG ---------- */
require __DIR__ . '/aichat_secrets.php';
$SYSTEM_PREAMBLE = "Respond in 250 words or fewer. No ** or * emphasis, no tables. Use paragraphs to separate distinct ideas. When mentioning a website, wrap the site name in an HTML link like <a href=\"https://example.com\">Example Site</a> — never show the raw URL. When quoting current or time-sensitive information, use your search tool to check live sources first.";

/* ---------- DATABASE ---------- */
$db = new SQLite3("chat.db");
$db->exec("
CREATE TABLE IF NOT EXISTS messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id TEXT,
    role TEXT,
    content TEXT,
    created DATETIME DEFAULT CURRENT_TIMESTAMP
)
");

/* ---------- GEMINI CALL ---------- */
function ask_gemini($text, $api_key, $system_instruction) {
    $url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.1-flash-lite:generateContent?key=" . $api_key;
    $prompt = $system_instruction . "\n\n" . $text;

    $data = [
        "contents" => [
            [
                "role" => "user",
                "parts" => [["text" => $prompt]]
            ]
        ],
        "tools" => [
            ["google_search" => new stdClass()]
        ]
    ];

    $ch = curl_init($url);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($ch, CURLOPT_HTTPHEADER, ["Content-Type: application/json"]);
    curl_setopt($ch, CURLOPT_POST, true);
    curl_setopt($ch, CURLOPT_POSTFIELDS, json_encode($data));

    $response = curl_exec($ch);
    if ($response === false) return "Curl error: " . curl_error($ch);
    curl_close($ch);

    $json = json_decode($response, true);
    if (isset($json["error"])) return "Gemini API error: " . $json["error"]["message"];

    return $json["candidates"][0]["content"]["parts"][0]["text"] ?? "No response.";
}

/* ---------- HANDLE USER MESSAGE ---------- */
if ($_SERVER["REQUEST_METHOD"] === "POST" && isset($_POST["msg"])) {
    $user = trim($_POST["msg"]);

    // Clear chat if user types 'new'
    if (strtolower($user) === "new") {
        $stmt = $db->prepare("DELETE FROM messages WHERE user_id=:uid");
        $stmt->bindValue(":uid", $user_id);
        $stmt->execute();
        header("Location: " . $_SERVER["PHP_SELF"]);
        exit;
    }

    if ($user !== "") {
        // store user message
        $stmt = $db->prepare("INSERT INTO messages(user_id, role, content) VALUES(:uid, :role, :c)");
        $stmt->bindValue(":uid", $user_id);
        $stmt->bindValue(":role", "user");
        $stmt->bindValue(":c", $user);
        $stmt->execute();

        // get last 10 messages for this user
        $res = $db->query("SELECT role,content FROM messages WHERE user_id='$user_id' ORDER BY id DESC LIMIT 10");
        $history = [];
        while ($row = $res->fetchArray(SQLITE3_ASSOC)) $history[] = $row;
        $history = array_reverse($history);

        $context = "";
        foreach ($history as $m) {
            $context .= ucfirst($m["role"]) . ": " . $m["content"] . "\n";
        }

        // call Gemini
        $reply = ask_gemini($context, $API_KEY, $SYSTEM_PREAMBLE);

        // store bot reply
        $stmt = $db->prepare("INSERT INTO messages(user_id, role, content) VALUES(:uid, :role, :c)");
        $stmt->bindValue(":uid", $user_id);
        $stmt->bindValue(":role", "assistant");
        $stmt->bindValue(":c", $reply);
        $stmt->execute();
    }
}

/* ---------- LOAD CHAT ---------- */
$messages = $db->query("SELECT role,content FROM messages WHERE user_id='$user_id' ORDER BY id");

/* ---------- SIMPLE DISPLAY ---------- */
function display_text($text, $allow_links = false) {
    if (!$allow_links) return htmlspecialchars($text);
    // Strip everything except <a> tags, then allow them through
    $safe = strip_tags($text, '<a>');
    // Escape any remaining HTML-unsafe chars outside of <a> tags
    // Split on <a...>...</a>, escape non-tag parts only
    return preg_replace_callback(
        '~(<a\s[^>]*>.*?</a>)|([^<]+|<)~is',
        function($m) {
            if ($m[1]) return $m[1]; // keep <a> tags as-is
            return htmlspecialchars($m[0]);
        },
        $safe
    );
}
?>

<!DOCTYPE html>
<html>
<head>
<title>Gemini Chatbot</title>
</head>
<body>

<div style="color:green;font-size: x-large;">Gemini Chatbot v0.4</div>
<p>Type new to clear the chat</p>

<div>
<?php
while ($row = $messages->fetchArray(SQLITE3_ASSOC)) {
    if ($row["role"] == "user") {
        // Entire user line bold and green
        echo "<div><b><span style='color:green;'>You: " . display_text($row["content"]) . "</span></b></div>";
    } else {
        // Bot line plain, block-level
        echo "<div>Bot: " . display_text($row["content"], true) . "</div>";
    }
}
?>
</div>
<hr>

<form method="post" action="<?php echo $_SERVER['PHP_SELF']; ?>">
    <input
        type="text"
        name="msg"
        placeholder="Type your message..."
        style="width:100%; height:28px;"
        autofocus
    >
</form>

</body>
</html>