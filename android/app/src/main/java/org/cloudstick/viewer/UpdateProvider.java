/* SPDX-License-Identifier: MIT */
package org.cloudstick.viewer;
import android.content.*;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import java.io.*;
/** A single read-only, temporarily granted APK; never general file access. */
public final class UpdateProvider extends ContentProvider {
    @Override public boolean onCreate(){return true;}
    @Override public String getType(Uri uri){return "application/vnd.android.package-archive";}
    @Override public ParcelFileDescriptor openFile(Uri uri,String mode)throws FileNotFoundException {
        if(!mode.equals("r")||!uri.getScheme().equals("content")||!uri.getAuthority().equals("org.cloudstick.viewer.updates")||!uri.getPath().equals("/update.apk")||uri.getQuery()!=null||uri.getFragment()!=null)throw new FileNotFoundException();
        return ParcelFileDescriptor.open(new File(getContext().getFilesDir(),"update.apk"),ParcelFileDescriptor.MODE_READ_ONLY);
    }
    @Override public Cursor query(Uri u,String[] p,String s,String[] a,String o){return null;}
    @Override public Uri insert(Uri u,ContentValues v){throw new UnsupportedOperationException();}
    @Override public int delete(Uri u,String s,String[] a){throw new UnsupportedOperationException();}
    @Override public int update(Uri u,ContentValues v,String s,String[] a){throw new UnsupportedOperationException();}
}
