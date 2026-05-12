<?php
/******************* Proxy for Babe32 browser  ***************

* 190326 New from Claude Code Babe32 project
*/

  $url = isset($_GET['url']) ? $_GET['url'] : '';
  if (!$url) { http_response_code(400); exit; }
  $ch = curl_init($url);
  curl_setopt_array($ch, [
      CURLOPT_RETURNTRANSFER => true,
      CURLOPT_FOLLOWLOCATION => true,
      CURLOPT_MAXREDIRS      => 5,
      CURLOPT_USERAGENT      => 'Mozilla/5.0 (compatible)',
      CURLOPT_TIMEOUT        => 15,
      CURLOPT_SSL_VERIFYPEER => false,
  ]);
  echo curl_exec($ch);
  curl_close($ch);
  
  ?>