#pragma once
#include <QString>

// «Jackpot»: звук и эффект выстрелов после снимка (шуточная функция, выключается в настройках).
namespace jackpot {

bool enabled();
int volume();           // 0..100
QString soundDir();     // сюда кладётся jackpot.ogg (или .wav, .opus, .flac, .mp3)
QString soundFile();    // найденный файл или пустая строка

// Проигрывает звук через pw-play, paplay, ffplay или mpv (что найдётся). vol < 0 — из настроек.
void playSound(int vol = -1);

// Звук + эффект на всех мониторах; возвращается, когда эффект закончился.
// force — показать, даже если функция выключена (для кнопки «Проверить»).
void celebrate(bool force = false, int vol = -1);

} // namespace jackpot