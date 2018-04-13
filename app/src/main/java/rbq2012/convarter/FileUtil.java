package rbq2012.convarter;

import android.content.res.AssetManager;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.InputStream;
import java.nio.charset.Charset;

import static android.content.res.AssetManager.ACCESS_BUFFER;

/**
 * Created by barco on 2018/3/14.
 */

public final class FileUtil {

    static final private String LOGTAG = "rbq2012-FileUtil";

    static final public String readTextFile(File file) {
        FileInputStream fis = null;
        String res = null;
        try {
            fis = new FileInputStream(file);
        } catch (Exception e) {
            Log.e(LOGTAG, "Failed in readTextFile: File can't be open.");
            e.printStackTrace();
            return null;
        }
        try {
            long len = file.length();
            if (len > 0x100000) {
                Log.e(LOGTAG, "Failed in readTextFile: File too large.");
            } else {
                int ilen = (int) len;
                byte[] bytes = new byte[ilen];
                fis.read(bytes, 0, ilen);
                res = new String(bytes, Charset.defaultCharset());
            }
        } catch (Exception e) {
            Log.e(LOGTAG, "Failed in readTextFile: File can't be read.");
            e.printStackTrace();
        } finally {
            try {
                fis.close();
            } catch (Exception e) {
            }
        }
        return res;
    }

    static final public boolean writeTextFile(File file, String content) {
        FileOutputStream fos = null;
        boolean res = true;
        try {
            fos = new FileOutputStream(file);
        } catch (Exception e) {
            Log.e(LOGTAG, "Failed in writeTextFile: File can't be open.");
            e.printStackTrace();
            return false;
        }
        try {
            byte[] bytes = content.getBytes(Charset.defaultCharset());
            fos.write(bytes);
        } catch (Exception e) {
            Log.e(LOGTAG, "Failed in writeTextFile: File can't be written.");
            e.printStackTrace();
            res = false;
        } finally {
            try {
                fos.close();
            } catch (Exception e) {
            }
        }
        return res;
    }

    public static String getAssetText(AssetManager mgr, String name) {
        try {
            InputStream is = mgr.open(name, ACCESS_BUFFER);
            StringBuilder sb = new StringBuilder();
            int ch;
            while ((ch = is.read()) != -1) {
                sb.append((char) ch);
            }
            return sb.toString();
        } catch (Exception e) {
            e.printStackTrace();
            return null;
        }
    }

}