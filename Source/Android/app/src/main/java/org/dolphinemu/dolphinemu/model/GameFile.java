package org.dolphinemu.dolphinemu.model;

import android.os.Environment;

public class GameFile
{
  private long mPointer;  // Do not rename or move without editing the native code

  private GameFile(long pointer)
  {
    mPointer = pointer;
  }

  @Override
  public native void finalize();

  public native int getPlatform();

  public native String getTitle();

  public native String getDescription();

  public native String getCompany();

  public native int getCountry();

  public native int getRegion();

  public native String getPath();

  public native String getGameId();

  public native int getDiscNumber();

  public native int getRevision();

  public native int[] getBanner();

  public native int getBannerWidth();

  public native int getBannerHeight();

  public String getCoverPath()
  {
    return Environment.getExternalStorageDirectory().getPath() +
            "/dolphin-zek/Cache/GameCovers/" + getGameId() + ".png";
  }

  public String getCustomCoverPath()
  {
    return getPath().substring(0, getPath().lastIndexOf(".")) + ".cover.png";
  }

  public String getScreenshotPath()
  {
    String gameId = getGameId();
    return "file://" + Environment.getExternalStorageDirectory().getPath() +
            "/dolphin-zek/ScreenShots/" + gameId + "/" + gameId + "-1.png";
  }
}
