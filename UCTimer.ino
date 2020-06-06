#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <MsTimer2.h>
#include <TimerOne.h>
#include <ClickEncoder.h>
#include <HT1621.h>

/* SDCard :
 *  0001: Unicorn (Auto Offset 40)
 *  0002: Beep (Auto Offset 0)
 *  0003-: Free 
 */

//DFPlayerと接続
SoftwareSerial dfSerial(9, 10);
DFRobotDFPlayerMini myDFPlayer;

//ロータリエンコーダ
ClickEncoder *enc1;
ClickEncoder *enc2;

//液晶の設定
HT1621 ht(2, 3, 4);
int htCode[] = {190, 6, 124, 94, 198, 218, 250, 14, 254, 222};

int16_t last1, val1, last2, val2, last1Tmp, last2Tmp;
int16_t swaplast1, swapval1, swaplast2, swapval2;

//画面モード記憶用
bool volSetting = false;
bool musicSetting = false;
bool offsetSetting = false;

//各種フラグ
bool isTimerWorking = false;
bool isLcdShowing = true; //for blink
bool isMusicPlaying = false;
bool fallback = true;

//音楽ファイルの設定
int musicNumber = 1;
int musicOffset = 41; //UCのサビ秒数
int musicCount;

int volume = 15;

//エラー処理用
int errNum;
bool error = false;

//ボタンチャタリング回避用
int btnRead = 0;
int oldBtnRead = 0;

//エンコーダ処理用割り込み
void timerIsr() {
  enc1->service();
  enc2->service();
}

//画面描画用
void writeHt(int d1, int d2, bool dot, bool d1null = false) {
  //一桁ずつにする
  int num1 = d1 / 10;
  int num2 = d1 % 10;
  int num3 = d2 / 10;
  int num4 = d2 % 10;

  //ひとけた目が非表示かどうかで分岐
  if (d1null) {
    ht.write(0, 0);
    if (dot) {
      ht.write(1, 1);
    } else {
      ht.write(1, 0);
    }
  } else {
    ht.write(0, htCode[num1]);
    if (dot) {
      ht.write(1, htCode[num2] + 1);
    } else {
      ht.write(1, htCode[num2]);
    }
  }
  ht.write(2, htCode[num3]);
  ht.write(3, htCode[num4]);
}

void setup() {
  register uint8_t i;
  dfSerial.begin(9600);
  ht.begin();

  //液晶イニシャライズ
  ht.sendCommand(HT1621::RC256K);
  ht.sendCommand(HT1621::BIAS_THIRD_4_COM);
  ht.sendCommand(HT1621::SYS_EN);
  ht.sendCommand(HT1621::LCD_ON);

  //液晶を空白にする
  for (i = 0; i < 16; i++) {
    ht.write(i, 0);
  }

  //ロータリエンコーダと接続
  enc1 = new ClickEncoder(A0, A1, A2);
  enc2 = new ClickEncoder(A3, A4, A5);

  //エンコーダの加速を無効にする
  enc1->setAccelerationEnabled(false);
  enc2->setAccelerationEnabled(false);

  pinMode(13, INPUT_PULLUP);

  //エンコーダ検知用の割り込みを設定
  Timer1.initialize(1000);
  Timer1.attachInterrupt(timerIsr);

  //チャタリング防止用
  last1 = 0;
  last2 = 0;

  //DFPlayerと接続、失敗したときはエラーで動作停止
  while (!myDFPlayer.begin(dfSerial)) {
    showErr(1);
  }

  //接続できたらエラー消す
  stopErr();

  //SDに曲が入ってないときはエラーで動作停止
  while ((musicCount = myDFPlayer.readFileCounts()) < 0) {
    showErr(2);
  }

  //入ってたらエラーを消す、一曲目を使うように設定
  stopErr();
  musicNumber = 1;

  //DFPlayerの設定
  myDFPlayer.volume(10);
  myDFPlayer.enableLoop();
  Serial.println(musicCount);  
}

void loop() {
  if (!isTimerWorking) { //タイマー計測中でないときのみ
    if (volSetting) { //ボリューム設定画面のとき
      //エンコーダの値を取得してボリュームを設定する
      int encVal1;
      val1 += enc1->getValue();

      //ボリュームが30よりも大きくなったら0に戻ってループ
      if (val1 / 4 > 30) {
        val1 = 0;
      } else if (val1 / 4 < 0) {
        val1 = 120; //30*4
      }

      //エンコーダの値が前回と変わってたらボリュームを反映
      if (val1 / 4 != last1) {
        encVal1 = val1 / 4;

        last1 = encVal1;
        volume = last1;
      }
      writeHt(0, last1, false, true);
    } else if (musicSetting) { //曲目設定画面のとき
      //エンコーダの値を取得して曲を設定する
      int encVal2;
      val2 += enc2->getValue();

      if (val2 / 4 > musicCount) {
        val2 = 1;
      } else if (val2 / 4 < 1) {
        val2 = musicCount * 4;
      }

      if (val2 / 4 != last2) {
        encVal2 = val2 / 4;

        last2 = encVal2;
        musicNumber = last2;
      }
      writeHt(0, last2, false, true);
    } else if (offsetSetting) { //曲オフセット設定画面のとき
      //エンコーダの値を取得してオフセットを設定する
      int encVal2;
      val2 += enc2->getValue();
      if (val2 / 4 > 99) {
        val2 = 0;
      } else if (val2 / 4 < 0) {
        val2 = 396; //99*4
      }

      if (val2 / 4 != last2) {
        encVal2 = val2 / 4;

        last2 = encVal2;
        musicOffset = last2;
      }
      writeHt(0, last2, false, true);
    } else { //設定画面が開かれていないとき
      int encVal1;
      int encVal2;
      val1 += enc1->getValue();
      val2 += enc2->getValue();

      //エンコーダで分の設定をする
      if (val1 / 4 > 99) {
        val1 = 0;
      } else if (val1 / 4 < 0) {
        val1 = 396; //99*4
      }

      //秒
      if (val2 / 4 > 59) {
        val2 = 0;
      } else if (val2 / 4 < 0) {
        val2 = 236; //59*4
      }

      if (val1 / 4 != last1) {
        encVal1 = val1 / 4;

        last1 = encVal1;
      }

      if (val2 / 4 != last2) {
        encVal2 = val2 / 4;

        last2 = encVal2;
      }
      writeHt(last1, last2, true);
    }
  }

  ClickEncoder::Button b = enc1->getButton();
  if (b != ClickEncoder::Open) {
    switch (b) {
      case ClickEncoder::Clicked:
        //ボタンが押された場合
        if (isTimerWorking) { //タイマーが動いてるときは止める
          isTimerWorking = false;
          myDFPlayer.stop();
          MsTimer2::stop();
          writeHt(last1, last2, true);
        } else { //タイマーを開始する
          if (!volSetting && !musicSetting && !offsetSetting) {
            isTimerWorking = true;
            if (musicOffset < last1 * 60 + last2) { //曲が設定時間よりも短い場合はFallback用の曲に変える
              fallback = false;
            } else {
              fallback = true;
            }
            Serial.println(last1 * 60 + last2);
            MsTimer2::set(1000, countDown); //一秒ごとにカウントダウン
            MsTimer2::start();
            last1Tmp = last1;
            last2Tmp = last2;
          }
        }
        break;

      case ClickEncoder::DoubleClicked:
        //ボタンがダブルクリックされたとき
        if (!isTimerWorking && !musicSetting && !offsetSetting) {
          if (volSetting) { //ボリューム設定画面から出る
            volSetting = false;
            volume = last1;
            last1 = swaplast1;
            val1 = swapval1;
            myDFPlayer.volume(volume);
          } else { //ボリューム設定画面に入る
            volSetting = true;
            swaplast1 = last1;
            swapval1 = val1;
            val1 = volume * 4;
            writeHt(0, volume, false, true);
          }
        }
        break;
    }
  }

  //エンコーダ2でも同様に処理
  ClickEncoder::Button b2 = enc2->getButton();
  if (b2 != ClickEncoder::Open) {
    switch (b2) {
      case ClickEncoder::Clicked:
        if (!isTimerWorking && !volSetting && !offsetSetting) {
          if (musicSetting) {
            musicSetting = false;
            musicNumber = last2;
            if(musicNumber == 1){
              musicOffset = 41;
            }else if(musicNumber == 2){
              musicOffset = 0;
            }
            last2 = swaplast2;
            val2 = swapval2;
          } else {
            musicSetting = true;
            swaplast2 = last2;
            swapval2 = val2;
            val2 = musicNumber * 4;
            writeHt(0, musicNumber, false, true);
          }
        }
        break;

      case ClickEncoder::DoubleClicked:
        if (!isTimerWorking && !musicSetting && !volSetting) {
          if (offsetSetting) {
            offsetSetting = false;
            musicOffset = last2;
            last2 = swaplast2;
            val2 = swapval2;
          } else {
            offsetSetting = true;
            swaplast2 = last2;
            swapval2 = val2;
            val2 = musicOffset * 4;
            writeHt(0, musicOffset, false, true);
          }
        }
        break;
    }
  }
}

void countDown() {
  last2Tmp--;
  if(last2Tmp == 0 && last1Tmp == 0){ //タイマー終了
      writeHt(0, 0, true);
      if (fallback) {
        myDFPlayer.loop(2);
      }
      MsTimer2::stop();
      MsTimer2::set(300, blinkLcd);
      MsTimer2::start();
      return;
  }
  if (last2Tmp < 0) { //分から繰り下がり
    if (last1Tmp > 0) {
      last1Tmp--;
      last2Tmp = 59;
    }
  }
  Serial.println(last1Tmp * 60 + last2Tmp);
  Serial.println(musicOffset);
  if (last1Tmp * 60 + last2Tmp == (musicOffset - 1) && !fallback) { //設定されたオフセットの時間になったら曲を流す
    myDFPlayer.loop(musicNumber);
  }
  writeHt(last1Tmp, last2Tmp, true);
}

//画面を点滅させる
void blinkLcd() {
  if (isLcdShowing) {
    for (int i = 0; i < 4; i++) {
      ht.write(i, 0);
    }
    isLcdShowing = false;
  } else {
    writeHt(0, 0, true);
    isLcdShowing = true;
  }
}

//エラー表示用関数
void showErr(int num) {
  if (error) return;
  error = true;
  errNum = num;
  ht.write(0, 248); //E
  ht.write(1, 96); //r
  ht.write(2, 97); //r.
  ht.write(3, htCode[num]);
  MsTimer2::set(300, blinkErr);
  MsTimer2::start();
}

void stopErr() {
  if (!error) return;
  error = false;
  MsTimer2::stop();
}

//showErr()から呼び出す。エラーを点滅させる。
void blinkErr() {
  if (isLcdShowing) {
    for (int i = 0; i < 16; i++) {
      ht.write(i, 0);
    }
    isLcdShowing = false;
  } else {
    ht.write(0, 248); //E
    ht.write(1, 96); //r
    ht.write(2, 97); //r.
    ht.write(3, htCode[errNum]);
    isLcdShowing = true;
  }
}
