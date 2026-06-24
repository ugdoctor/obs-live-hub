# RELEASE_CHECKLIST.md
# git push / GitHub Release 作成前に必ず確認すること

## 1. 追跡ファイルの確認
- [ ] git ls-files で追跡中のファイル一覧を確認
- [ ] config.json（本番認証情報）が含まれていないか
- [ ] CSVファイル（tts_settings.csv, points_actions等の実データ）が
      含まれていないか
- [ ] .gitignore に除外漏れがないか

## 2. コミット履歴の確認
- [ ] git log --all --full-history -- "*.json" "*.csv" で過去に
      機密ファイルがコミットされていないか確認
- [ ] 過去のコミットにOAuthトークン・APIキー等の「値」が直接
      ログ出力・ハードコードされていないか確認
      （下記コマンドを全コミット履歴に対して実行するか、
      該当コミットのdiffを個別に確認）

      スネークケース・キャメルケース両対応（-i で大文字小文字も無視）:
      git log -p | grep -iE "(client_secret|clientSecret|api_key|apiKey|api_secret|apiSecret|oauth_token|oauthToken|access_token|accessToken|token_secret|tokenSecret|refresh_token|refreshToken)" | grep -v "^[+-][+-][+-]" | grep -v "^diff\|^index\|^---\|^+++"

- [ ] コードのロジックのみが履歴に残るのは問題なし。
      「値」が残っている場合のみ要対応（履歴の書き換えを検討）

## 3. ソースコード内の直接埋め込み確認
- [ ] 以下のコマンドでハードコードされた値がないか確認
      （スネークケース・キャメルケース・大文字小文字をすべてカバー）:

      grep -rniE "(client_secret|clientSecret|api_key|apiKey|api_secret|apiSecret|oauth_token|oauthToken|access_token|accessToken|token_secret|tokenSecret|refresh_token|refreshToken)" src/

      ※ 変数名・フィールド名・コメントとして出力されることは正常。
        文字列リテラルに「値そのもの」が埋め込まれていないかを確認すること。

## 4. ログ出力コードの確認
- [ ] OAuth関連のレスポンス処理で、レスポンスボディ全文や
      トークン値そのものをログ出力していないか確認
      （存在有無のtrue/falseのみ出力するのが安全）

## 5. ドキュメント内の確認
- [ ] STATUS.md / CLAUDE.md / CLAUDE_LOG.md に実際のトークン値、
      チャンネルID等の個人情報が記載されていないか確認

## 6. ビルド確認
- [ ] クリーンビルドを実行し、最新のコードが正しく反映されることを
      確認（ビルドディレクトリの古いキャッシュ起因の問題を防ぐ）

## 結果の分類
各項目について、問題が見つかった場合は以下のいずれかに分類して報告：
- 現状追跡されていないので問題なし
- 現状は追跡されていないが過去のコミット履歴に残っている（値が残っているか要確認）
- 現在も追跡されており要対応
